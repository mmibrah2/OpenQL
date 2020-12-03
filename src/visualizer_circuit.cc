/** \file
 * Definition of the visualizer.
 */

#ifdef WITH_VISUALIZER

#include "visualizer.h"
#include "visualizer_common.h"
#include "visualizer_circuit.h"
#include "utils/json.h"

namespace ql {

using namespace utils;

// ======================================================= //
// =                     CircuitData                     = //
// ======================================================= //

CircuitData::CircuitData(Vec<GateProperties> &gates, const Layout &layout, const Int cycleDuration) :
    cycles(generateCycles(gates, cycleDuration)),
    amountOfQubits(calculateAmountOfBits(gates, &GateProperties::operands)),
    amountOfClassicalBits(calculateAmountOfBits(gates, &GateProperties::creg_operands)),
    cycleDuration(cycleDuration)
{
    if (layout.cycles.areCompressed())      compressCycles();
    if (layout.cycles.arePartitioned())     partitionCyclesWithOverlap();
    if (layout.cycles.cutting.isEnabled())  cutEmptyCycles(layout);
}

Int CircuitData::calculateAmountOfCycles(const Vec<GateProperties> &gates, const Int cycleDuration) const {
    QL_DOUT("Calculating amount of cycles...");

    // Find the highest cycle in the gate vector.
    Int amountOfCycles = 0;
    for (const GateProperties &gate : gates) {
        const Int gateCycle = gate.cycle;
        if (gateCycle < 0 || gateCycle > MAX_ALLOWED_VISUALIZER_CYCLE) {
            QL_FATAL("Found gate with cycle index: " << gateCycle << ". Only indices between 0 and " 
               << MAX_ALLOWED_VISUALIZER_CYCLE << " are allowed!"
               << "\nMake sure gates are scheduled before calling the visualizer pass!");
        }
        if (gateCycle > amountOfCycles)
            amountOfCycles = gateCycle;
    }

    // The last gate requires a different approach, because it might have a
    // duration of multiple cycles. None of those cycles will show up as cycle
    // index on any other gate, so we need to calculate them seperately.
    const Int lastGateDuration = gates.at(gates.size() - 1).duration;
    const Int lastGateDurationInCycles = lastGateDuration / cycleDuration;
    if (lastGateDurationInCycles > 1)
        amountOfCycles += lastGateDurationInCycles - 1;

    // Cycles start at zero, so we add 1 to get the true amount of cycles.
    return amountOfCycles + 1; 
}

Vec<Cycle> CircuitData::generateCycles(Vec<GateProperties> &gates, const Int cycleDuration) const {
    QL_DOUT("Generating cycles...");

    // Generate the cycles.
    Vec<Cycle> cycles;
    const Int amountOfCycles = calculateAmountOfCycles(gates, cycleDuration);
    for (Int i = 0; i < amountOfCycles; i++) {
        // Generate the first chunk of the gate partition for this cycle.
        // All gates in this cycle will be added to this chunk first, later on
        // they will be divided based on connectivity (if enabled).
        Vec<Vec<std::reference_wrapper<GateProperties>>> partition;
        const Vec<std::reference_wrapper<GateProperties>> firstChunk;
        partition.push_back(firstChunk);

        cycles.push_back({i, true, false, partition});
    }
    // Mark non-empty cycles and add gates to their corresponding cycles.
    for (GateProperties &gate : gates) {
        cycles[gate.cycle].empty = false;
        cycles[gate.cycle].gates[0].push_back(gate);
    }

    return cycles;
}

void CircuitData::compressCycles() {
    QL_DOUT("Compressing circuit...");

    // Each non-empty cycle will be added to a new vector. Those cycles will
    // have their index (and the cycle indices of its gates) updated to reflect
    // the position in the compressed cycles vector.
    Vec<Cycle> compressedCycles;
    Int amountOfCompressions = 0;
    for (UInt i = 0; i < cycles.size(); i++) {
        // Add each non-empty cycle to the vector and update its relevant
        // attributes.
        if (!cycles[i].empty) {
            Cycle &cycle = cycles[i];
            cycle.index = utoi(i) - amountOfCompressions;
            // Update the gates in the cycle with the new cycle index.
            for (UInt j = 0; j < cycle.gates.size(); j++) {
                for (GateProperties &gate : cycle.gates[j]) {
                    gate.cycle -= amountOfCompressions;
                }
            }
            compressedCycles.push_back(cycle);
        } else {
            amountOfCompressions++;
        }
    }

    cycles = compressedCycles;
}

void CircuitData::partitionCyclesWithOverlap()
{
    QL_DOUT("Partioning cycles with connections overlap...");

    // Find cycles with overlapping connections.
    for (Cycle &cycle : cycles) {
        if (cycle.gates[0].size() > 1) {
            // Find the multi-operand gates in this cycle.
            Vec<std::reference_wrapper<GateProperties>> candidates;
            for (GateProperties &gate : cycle.gates[0]) {
                if (gate.operands.size() + gate.creg_operands.size() > 1) {
                    candidates.push_back(gate);
                }
            }

            // If more than one multi-operand gate has been found in this cycle,
            // check if any of those gates overlap.
            if (candidates.size() > 1) {
                Vec<Vec<std::reference_wrapper<GateProperties>>> partition;
                for (GateProperties &candidate : candidates) {
                    // Check if the gate can be placed in an existing chunk.
                    Bool placed = false;
                    for (Vec<std::reference_wrapper<GateProperties>> &chunk : partition) {
                        // Check if the gate overlaps with any other gate in the
                        // chunk.
                        Bool gateOverlaps = false;
                        const Pair<GateOperand, GateOperand> edgeOperands1 = calculateEdgeOperands(getGateOperands(candidate), amountOfQubits);
                        for (const GateProperties &gateInChunk : chunk) {
                            const Pair<GateOperand, GateOperand> edgeOperands2 = calculateEdgeOperands(getGateOperands(gateInChunk), amountOfQubits);
                            if ((edgeOperands1.first >= edgeOperands2.first && edgeOperands1.first <= edgeOperands2.second) ||
                                (edgeOperands1.second >= edgeOperands2.first && edgeOperands1.second <= edgeOperands2.second))
                            {
                                gateOverlaps = true;
                            }
                        }

                        // If the gate does not overlap with any gate in the
                        // chunk, add the gate to the chunk.
                        if (!gateOverlaps) {
                            chunk.push_back(candidate);
                            placed = true;
                            break;
                        }
                    }

                    // If the gate has not been added to the chunk, add it to
                    // the partition in a new chunk.
                    if (!placed) {
                        partition.push_back({candidate});
                    }
                }

                // If the partition has more than one chunk, we replace the
                // original partition in the current cycle.
                if (partition.size() > 1) {
                    QL_DOUT("Divided cycle " << cycle.index << " Into " << partition.size() << " chunks:");
                    for (UInt i = 0; i < partition.size(); i++) {
                        QL_DOUT("Gates in chunk " << i << ":");
                        for (const GateProperties &gate : partition[i]) {
                            QL_DOUT("\t" << gate.name);
                        }
                    }

                    cycle.gates = partition;
                }
            }
        }
    }
}

void CircuitData::cutEmptyCycles(const Layout &layout) {
    QL_DOUT("Cutting empty cycles...");

    if (layout.pulses.areEnabled()) {
        //TODO: an empty cycle as defined in pulse visualization is a cycle in
        //      which no lines for each qubit have a pulse going
        //TODO: implement checking for the above and mark those cycles as cut

        QL_WOUT("Cycle cutting is not yet implemented for pulse visualization.");
        return;
    }

    // Find cuttable ranges...
    cutCycleRangeIndices = findCuttableEmptyRanges(layout);
    // ... and cut them.
    for (const EndPoints &range : cutCycleRangeIndices) {
        for (Int i = range.start; i <= range.end; i++) {
            cycles[i].cut = true;
        }
    }
}

Vec<EndPoints> CircuitData::findCuttableEmptyRanges(const Layout &layout) const {
    QL_DOUT("Finding cuttable empty cycle ranges...");

    // Calculate the empty cycle ranges.
    Vec<EndPoints> ranges;
    for (UInt i = 0; i < cycles.size(); i++) {
        // If an empty cycle has been found...
        if (cycles[i].empty) {
            const Int start = utoi(i);
            Int end = utoi(cycles.size()) - 1;

            UInt j = i;
            // ... add cycles to the range until a non-empty cycle is found.
            while (j < cycles.size()) {
                if (!cycles[j].empty) {
                    end = utoi(j) - 1;
                    break;
                }
                j++;
            }
            ranges.push_back( {start, end} );

            // Skip over the found range.
            i = j;
        }
    }

    // Check for empty cycle ranges above the threshold.
    Vec<EndPoints> rangesAboveThreshold;
    for (const auto &range : ranges) {
        const Int length = range.end - range.start + 1;
        if (length >= layout.cycles.cutting.getEmptyCycleThreshold()) {
            rangesAboveThreshold.push_back(range);
        }
    }

    return rangesAboveThreshold;
}

Cycle CircuitData::getCycle(const UInt index) const {
    if (index > cycles.size())
        QL_FATAL("Requested cycle index " << index << " is higher than max cycle " << (cycles.size() - 1) << "!");

    return cycles[index];
}

Int CircuitData::getAmountOfCycles() const {
    return utoi(cycles.size());
}

Bool CircuitData::isCycleCut(const Int cycleIndex) const {
    return cycles[cycleIndex].cut;
}

Bool CircuitData::isCycleFirstInCutRange(const Int cycleIndex) const {
    for (const EndPoints &range : cutCycleRangeIndices) {
        if (cycleIndex == range.start) {
            return true;
        }
    }

    return false;
}

void CircuitData::printProperties() const {
    QL_DOUT("[CIRCUIT DATA PROPERTIES]");

    QL_DOUT("amountOfQubits: " << amountOfQubits);
    QL_DOUT("amountOfClassicalBits: " << amountOfClassicalBits);
    QL_DOUT("cycleDuration: " << cycleDuration);

    QL_DOUT("cycles:");
    for (UInt cycle = 0; cycle < cycles.size(); cycle++) {
        QL_DOUT("\tcycle: " << cycle << " empty: " << cycles[cycle].empty << " cut: " << cycles[cycle].cut);
    }

    QL_DOUT("cutCycleRangeIndices");
    for (const auto &range : cutCycleRangeIndices)
    {
        QL_DOUT("\tstart: " << range.start << " end: " << range.end);
    }
}

// ======================================================= //
// =                      Structure                      = //
// ======================================================= //

Structure::Structure(const Layout &layout, const CircuitData &circuitData) :
    layout(layout),
    cellDimensions({layout.grid.getCellSize(), calculateCellHeight(layout)}),
    cycleLabelsY(layout.grid.getBorderSize()),
    bitLabelsX(layout.grid.getBorderSize())
{
    generateCellPositions(circuitData);
    generateBitLineSegments(circuitData);

    imageWidth = calculateImageWidth(circuitData);
    imageHeight = calculateImageHeight(circuitData);
}

Int Structure::calculateCellHeight(const Layout &layout) const {
    QL_DOUT("Calculating cell height...");

    if (layout.pulses.areEnabled()) {
        return layout.pulses.getPulseRowHeightMicrowave() 
               + layout.pulses.getPulseRowHeightFlux()
               + layout.pulses.getPulseRowHeightReadout();
    } else {
        return layout.grid.getCellSize();
    }
}

Int Structure::calculateImageWidth(const CircuitData &circuitData) const {
    QL_DOUT("Calculating image width...");

    const Int amountOfCells = utoi(qbitCellPositions.size());
    const Int left = amountOfCells > 0 ? getCellPosition(0, 0, QUANTUM).x0 : 0;
    const Int right = amountOfCells > 0 ? getCellPosition(amountOfCells - 1, 0, QUANTUM).x1 : 0;
    const Int imageWidthFromCells = right - left;

    return layout.bitLines.labels.getColumnWidth() + imageWidthFromCells + layout.grid.getBorderSize() * 2;
}

Int Structure::calculateImageHeight(const CircuitData &circuitData) const {
    QL_DOUT("Calculating image height...");
    
    const Int rowsFromQuantum = circuitData.amountOfQubits;
    // Here be nested ternary operators.
    const Int rowsFromClassical = 
        layout.bitLines.classical.isEnabled()
            ? (layout.bitLines.classical.isGrouped() ? (circuitData.amountOfClassicalBits > 0 ? 1 : 0) : circuitData.amountOfClassicalBits)
            : 0;
    const Int heightFromOperands = 
        (rowsFromQuantum + rowsFromClassical) *
        (cellDimensions.height + (layout.bitLines.edges.areEnabled() ? layout.bitLines.edges.getThickness() : 0));

    return layout.cycles.labels.getRowHeight() + heightFromOperands + layout.grid.getBorderSize() * 2;
}

void Structure::generateCellPositions(const CircuitData &circuitData) {
    QL_DOUT("Generating cell positions...");

    // Calculate cell positions.
    Int widthFromCycles = 0;
    for (Int column = 0; column < circuitData.getAmountOfCycles(); column++) {
        const Int amountOfChunks = utoi(circuitData.getCycle(column).gates.size());
        const Int cycleWidth = (circuitData.isCycleCut(column) ? layout.cycles.cutting.getCutCycleWidth() : (cellDimensions.width * amountOfChunks));

        const Int x0 = layout.grid.getBorderSize() + layout.bitLines.labels.getColumnWidth() + widthFromCycles;
        const Int x1 = x0 + cycleWidth;

        // Quantum cell positions.
        Vec<Position4> qColumnCells;
        for (Int row = 0; row < circuitData.amountOfQubits; row++) {
            const Int y0 = layout.grid.getBorderSize() + layout.cycles.labels.getRowHeight() +
                row * (cellDimensions.height + (layout.bitLines.edges.areEnabled() ? layout.bitLines.edges.getThickness() : 0));
            const Int y1 = y0 + cellDimensions.height;
            qColumnCells.push_back({x0, y0, x1, y1});
        }
        qbitCellPositions.push_back(qColumnCells);
        // Classical cell positions.
        Vec<Position4> cColumnCells;
        for (Int row = 0; row < circuitData.amountOfClassicalBits; row++) {
            const Int y0 = layout.grid.getBorderSize() + layout.cycles.labels.getRowHeight() + 
                ((layout.bitLines.classical.isGrouped() ? 0 : row) + circuitData.amountOfQubits) *
                (cellDimensions.height + (layout.bitLines.edges.areEnabled() ? layout.bitLines.edges.getThickness() : 0));
            const Int y1 = y0 + cellDimensions.height;
            cColumnCells.push_back({x0, y0, x1, y1});
        }
        cbitCellPositions.push_back(cColumnCells);

        // Add the appropriate amount of width to the total width.
        if (layout.cycles.cutting.isEnabled()) {
            if (circuitData.isCycleCut(column)) {
                if (column != circuitData.getAmountOfCycles() - 1 && !circuitData.isCycleCut(column + 1)) {
                    widthFromCycles += (Int) (cellDimensions.width * layout.cycles.cutting.getCutCycleWidthModifier());
                }
            } else {
                widthFromCycles += cycleWidth;
            }
        } else {
            widthFromCycles += cycleWidth;
        }
    }
}

void Structure::generateBitLineSegments(const CircuitData &circuitData) {
    QL_DOUT("Generating bit line segments...");

    // Calculate the bit line segments.
    for (Int i = 0; i < circuitData.getAmountOfCycles(); i++) {
        const Bool cut = circuitData.isCycleCut(i);
        Bool reachedEnd = false;

        // Add more cycles to the segment until we reach a cycle that is cut if
        // the current segment is not cut, or vice versa.
        for (Int j = i; j < circuitData.getAmountOfCycles(); j++) {
            if (circuitData.isCycleCut(j) != cut) {
                const Int start = getCellPosition(i, 0, QUANTUM).x0;
                const Int end = getCellPosition(j, 0, QUANTUM).x0;
                bitLineSegments.push_back({{start, end}, cut});
                i = j - 1;
                break;
            }

            // Check if the last cycle has been reached, and exit the
            // calculation if so.
            if (j == circuitData.getAmountOfCycles() - 1) {
                const Int start = getCellPosition(i, 0, QUANTUM).x0;
                const Int end = getCellPosition(j, 0, QUANTUM).x1;
                bitLineSegments.push_back({{start, end}, cut});
                reachedEnd = true;
            }
        }
        
        if (reachedEnd) break;
    }
}

Int Structure::getImageWidth() const {
    return imageWidth;
}

Int Structure::getImageHeight() const {
    return imageHeight;
}

Int Structure::getCycleLabelsY() const {
    return cycleLabelsY;
}

Int Structure::getBitLabelsX() const {
    return bitLabelsX;
}

Int Structure::getCircuitTopY() const {
    return cycleLabelsY;
}

Int Structure::getCircuitBotY() const {
    const Vec<Position4> firstColumnPositions = layout.pulses.areEnabled() ? qbitCellPositions[0] : cbitCellPositions[0];
    Position4 botPosition = firstColumnPositions[firstColumnPositions.size() - 1];
    return botPosition.y1;
}

Dimensions Structure::getCellDimensions() const {
    return cellDimensions;
}

Position4 Structure::getCellPosition(const UInt column, const UInt row, const BitType bitType) const {
    switch (bitType) {
        case CLASSICAL:
            if (layout.pulses.areEnabled())
                QL_FATAL("Cannot get classical cell position when pulse visualization is enabled!");
            if (column >= cbitCellPositions.size())
                QL_FATAL("cycle " << column << " is larger than max cycle " << cbitCellPositions.size() - 1 << " of structure!");
            if (row >= cbitCellPositions[column].size())
                QL_FATAL("classical operand " << row << " is larger than max operand " << cbitCellPositions[column].size() - 1 << " of structure!");
            return cbitCellPositions[column][row];    

        case QUANTUM:
            if (column >= qbitCellPositions.size())
                QL_FATAL("cycle " << column << " is larger than max cycle " << qbitCellPositions.size() - 1 << " of structure!");
            if (row >= qbitCellPositions[column].size())
                QL_FATAL("quantum operand " << row << " is larger than max operand " << qbitCellPositions[column].size() - 1 << " of structure!");
            return qbitCellPositions[column][row];

        default:
            QL_FATAL("Unknown bit type!");
    }
}

Vec<Pair<EndPoints, Bool>> Structure::getBitLineSegments() const {
    return bitLineSegments;
}

void Structure::printProperties() const {
    QL_DOUT("[STRUCTURE PROPERTIES]");

    QL_DOUT("imageWidth: " << imageWidth);
    QL_DOUT("imageHeight: " << imageHeight);

    QL_DOUT("cycleLabelsY: " << cycleLabelsY);
    QL_DOUT("bitLabelsX: " << bitLabelsX);

    QL_DOUT("qbitCellPositions:");
    for (UInt cycle = 0; cycle < qbitCellPositions.size(); cycle++) {
        for (UInt operand = 0; operand < qbitCellPositions[cycle].size(); operand++) {
            QL_DOUT("\tcell: [" << cycle << "," << operand << "]"
                << " x0: " << qbitCellPositions[cycle][operand].x0
                << " x1: " << qbitCellPositions[cycle][operand].x1
                << " y0: " << qbitCellPositions[cycle][operand].y0
                << " y1: " << qbitCellPositions[cycle][operand].y1);
        }
    }

    QL_DOUT("cbitCellPositions:");
    for (UInt cycle = 0; cycle < cbitCellPositions.size(); cycle++) {
        for (UInt operand = 0; operand < cbitCellPositions[cycle].size(); operand++) {
            QL_DOUT("\tcell: [" << cycle << "," << operand << "]"
                << " x0: " << cbitCellPositions[cycle][operand].x0
                << " x1: " << cbitCellPositions[cycle][operand].x1
                << " y0: " << cbitCellPositions[cycle][operand].y0
                << " y1: " << cbitCellPositions[cycle][operand].y1);
        }
    }

    QL_DOUT("bitLineSegments:");
    for (const auto &segment : bitLineSegments) {
        QL_DOUT("\tcut: " << segment.second << " start: " << segment.first.start << " end: " << segment.first.end);
    }
}

void visualizeCircuit(Vec<GateProperties> gates, const Layout &layout, const Int cycleDuration, const Str &waveformMappingPath)
{
    // Initialize the circuit properties.
    CircuitData circuitData(gates, layout, cycleDuration);
    circuitData.printProperties();
    
    // Initialize the structure of the visualization.
    QL_DOUT("Initializing visualization structure...");
    Structure structure(layout, circuitData);
    structure.printProperties();
    
    // Initialize image.
    QL_DOUT("Initializing image...");
    const Int numberOfChannels = 3;
    cimg_library::CImg<Byte> image(structure.getImageWidth(), structure.getImageHeight(), 1, numberOfChannels);
    image.fill(255);

    // Draw the cycle labels if the option has been set.
    if (layout.cycles.labels.areEnabled()) {
        drawCycleLabels(image, layout, circuitData, structure);
    }

    // Draw the cycle edges if the option has been set.
    if (layout.cycles.edges.areEnabled()) {
        drawCycleEdges(image, layout, circuitData, structure);
    }

    // Draw the bit line edges if enabled.
    if (layout.bitLines.edges.areEnabled()) {
        drawBitLineEdges(image, layout, circuitData, structure);
    }

    // Draw the bit line labels if enabled.
    if (layout.bitLines.labels.areEnabled()) {
        drawBitLineLabels(image, layout, circuitData, structure);
    }

    // Draw the circuit as pulses if enabled.
    if (layout.pulses.areEnabled()) {
        PulseVisualization pulseVisualization = parseWaveformMapping(waveformMappingPath);
        const Vec<QubitLines> linesPerQubit = generateQubitLines(gates, pulseVisualization, circuitData);

        // Draw the lines of each qubit.
        QL_DOUT("Drawing qubit lines for pulse visualization...");
        for (Int qubitIndex = 0; qubitIndex < circuitData.amountOfQubits; qubitIndex++) {
            const Int yBase = structure.getCellPosition(0, qubitIndex, QUANTUM).y0;

            drawLine(image, structure, cycleDuration, linesPerQubit[qubitIndex].microwave, qubitIndex,
                yBase,
                layout.pulses.getPulseRowHeightMicrowave(),
                layout.pulses.getPulseColorMicrowave());

            drawLine(image, structure, cycleDuration, linesPerQubit[qubitIndex].flux, qubitIndex,
                yBase + layout.pulses.getPulseRowHeightMicrowave(),
                layout.pulses.getPulseRowHeightFlux(),
                layout.pulses.getPulseColorFlux());

            drawLine(image, structure, cycleDuration, linesPerQubit[qubitIndex].readout, qubitIndex,
                yBase + layout.pulses.getPulseRowHeightMicrowave() + layout.pulses.getPulseRowHeightFlux(),
                layout.pulses.getPulseRowHeightReadout(),
                layout.pulses.getPulseColorReadout());
        }

        // // Visualize the gates as pulses on a microwave, flux and readout line.
        // if (layout.pulses.displayGatesAsPulses)
        // {
        //     // Only draw wiggles if the cycle is cut.
        //     if (circuitData.isCycleCut(cycle.index))
        //     {
        //         for (Int qubitIndex = 0; qubitIndex < circuitData.amountOfQubits; qubitIndex++)
        //         {
        //             const Position4 cellPosition = structure.getCellPosition(cycle.index, qubitIndex, QUANTUM);
                    
        //             // Draw wiggle on microwave line.
        //             drawWiggle(image,
        //                        cellPosition.x0,
        //                        cellPosition.x1,
        //                        cellPosition.y0 + layout.pulses.pulseRowHeightMicrowave / 2,
        //                        cellPosition.x1 - cellPosition.x0,
        //                        layout.pulses.pulseRowHeightMicrowave / 8,
        //                        layout.pulses.pulseColorMicrowave);
                    
        //             // Draw wiggle on flux line.
        //             drawWiggle(image,
        //                        cellPosition.x0,
        //                        cellPosition.x1,
        //                        cellPosition.y0 + layout.pulses.pulseRowHeightMicrowave + layout.pulses.pulseRowHeightFlux / 2,
        //                        cellPosition.x1 - cellPosition.x0,
        //                        layout.pulses.pulseRowHeightFlux / 8,
        //                        layout.pulses.pulseColorFlux);
                    
        //             // Draw wiggle on readout line.
        //             drawWiggle(image,
        //                        cellPosition.x0,
        //                        cellPosition.x1,
        //                        cellPosition.y0 + layout.pulses.pulseRowHeightMicrowave + layout.pulses.pulseRowHeightFlux + layout.pulses.pulseRowHeightReadout / 2,
        //                        cellPosition.x1 - cellPosition.x0,
        //                        layout.pulses.pulseRowHeightReadout / 8,
        //                        layout.pulses.pulseColorReadout);
        //         }
                
        //         return;
        //     }
    } else {
        // Pulse visualization is not enabled, so we draw the circuit as an abstract entity.

        // Draw the quantum bit lines.
        QL_DOUT("Drawing qubit lines...");
        for (Int i = 0; i < circuitData.amountOfQubits; i++) {
            drawBitLine(image, layout, QUANTUM, i, circuitData, structure);
        }
            
        // Draw the classical lines if enabled.
        if (layout.bitLines.classical.isEnabled()) {
            // Draw the grouped classical bit lines if the option is set.
            if (circuitData.amountOfClassicalBits > 0 && layout.bitLines.classical.isGrouped()) {
                drawGroupedClassicalBitLine(image, layout, circuitData, structure);
            } else {
                // Otherwise draw each classical bit line seperate.
                QL_DOUT("Drawing ungrouped classical bit lines...");
                for (Int i = 0; i < circuitData.amountOfClassicalBits; i++) {
                    drawBitLine(image, layout, CLASSICAL, i, circuitData, structure);
                }
            }
        }

        // Draw the cycles.
        QL_DOUT("Drawing cycles...");
        for (Int i = 0; i < circuitData.getAmountOfCycles(); i++) {
            // Only draw a cut cycle if its the first in its cut range.
            if (circuitData.isCycleCut(i)) {
                if (i > 0 && !circuitData.isCycleCut(i - 1)) {
                    drawCycle(image, layout, circuitData, structure, circuitData.getCycle(i));
                }
            } else {
                // If the cycle is not cut, just draw it.
                drawCycle(image, layout, circuitData, structure, circuitData.getCycle(i));
            }
        }
    }

    // Display the image.
    QL_DOUT("Displaying image...");
    image.display("Quantum Circuit");
}

PulseVisualization parseWaveformMapping(const Str &waveformMappingPath) {
    QL_DOUT("Parsing waveform mapping configuration file...");

    // Read the waveform mapping Json file.
    Json waveformMapping;
    try {
        waveformMapping = load_json(waveformMappingPath);
    } catch (Json::exception &e) {
        QL_FATAL("Failed to load the visualization waveform mapping file:\n\t" << Str(e.what()));
    }

    PulseVisualization pulseVisualization;

    // Parse the sample rates.
    if (waveformMapping.count("samplerates") == 1) {
        try {
            if (waveformMapping["samplerates"].count("microwave") == 1)
                pulseVisualization.sampleRateMicrowave = waveformMapping["samplerates"]["microwave"];
            else
                QL_FATAL("Missing 'samplerateMicrowave' attribute in waveform mapping file!");

            if (waveformMapping["samplerates"].count("flux") == 1)
                pulseVisualization.sampleRateFlux = waveformMapping["samplerates"]["flux"];
            else
                QL_FATAL("Missing 'samplerateFlux' attribute in waveform mapping file!");

            if (waveformMapping["samplerates"].count("readout") == 1)
                pulseVisualization.sampleRateReadout = waveformMapping["samplerates"]["readout"];
            else
                QL_FATAL("Missing 'samplerateReadout' attribute in waveform mapping file!");
        } catch (std::exception &e) {
            QL_FATAL("Exception while parsing sample rates from waveform mapping file:\n\t" << e.what()
                 << "\n\tMake sure the sample rates are Integers!" );
        }
    } else {
        QL_FATAL("Missing 'samplerates' attribute in waveform mapping file!");
    }

    // Parse the codeword mapping.
    if (waveformMapping.count("codewords") == 1) {
        // For each codeword...
        for (const auto &codewordMapping : waveformMapping["codewords"].items()) {
            // ... get the index and the qubit pulse mappings it contains.
            Int codewordIndex = 0;
            try {
                codewordIndex = parse_int(codewordMapping.key());
            } catch (std::exception &e) {
                QL_FATAL("Exception while parsing key to codeword mapping " << codewordMapping.key()
                     << " in waveform mapping file:\n\t" << e.what() << "\n\tKey should be an Integer!");
            }
            Map<Int, GatePulses> qubitMapping;

            // For each qubit in the codeword...
            for (const auto &qubitMap : codewordMapping.value().items()) {
                // ... get the index and the pulse mapping.
                Int qubitIndex = 0;
                try {
                    qubitIndex = parse_int(qubitMap.key());
                } catch (std::exception &e) {
                    QL_FATAL("Exception while parsing key to qubit mapping " << qubitMap.key() << " in waveform mapping file:\n\t"
                         << e.what() << "\n\tKey should be an Integer!");
                }
                auto gatePulsesMapping = qubitMap.value();

                // Read the pulses from the pulse mapping.
                Vec<Real> microwave;
                Vec<Real> flux;
                Vec<Real> readout;
                try {
                    if (gatePulsesMapping.contains("microwave")) microwave = gatePulsesMapping["microwave"].get<Vec<Real>>();
                    if (gatePulsesMapping.contains("flux")) flux = gatePulsesMapping["flux"].get<Vec<Real>>();
                    if (gatePulsesMapping.contains("readout")) readout = gatePulsesMapping["readout"].get<Vec<Real>>();
                } catch (std::exception &e) {
                    QL_FATAL("Exception while parsing waveforms from waveform mapping file:\n\t" << e.what()
                         << "\n\tMake sure the waveforms are arrays of Integers!" );
                }
                GatePulses gatePulses {microwave, flux, readout};

                // Insert the pulse mapping Into the qubit.
                qubitMapping.insert({qubitIndex, gatePulses});
            }

            // Insert the mapping for the qubits Into the codeword.
            pulseVisualization.mapping.insert({codewordIndex, qubitMapping});
        }
    } else {
        QL_FATAL("Missing 'codewords' attribute in waveform mapping file!");
    }

    // // PrInt the waveform mapping.
    // for (const Pair<Int, Map<Int, GatePulses>>& codeword : pulseVisualization.mapping)
    // {
    //     IOUT("codeword: " << codeword.first);
    //     for (const Pair<Int, GatePulses>& gatePulsesMapping : codeword.second)
    //     {
    //         const Int qubitIndex = gatePulsesMapping.first;
    //         IOUT("\tqubit: " << qubitIndex);
    //         const GatePulses gatePulses = gatePulsesMapping.second;

    //         Str microwaveString = "[ ";
    //         for (const Int amplitude : gatePulses.microwave)
    //         {
    //             microwaveString += to_string(amplitude) + " ";
    //         }
    //         microwaveString += "]";
    //         IOUT("\t\tmicrowave: " << microwaveString);

    //         Str fluxString = "[ ";
    //         for (const Int amplitude : gatePulses.flux)
    //         {
    //             fluxString += to_string(amplitude) + " ";
    //         }
    //         fluxString += "]";
    //         IOUT("\t\tflux: " << fluxString);

    //         Str readoutString = "[ ";
    //         for (const Int amplitude : gatePulses.readout)
    //         {
    //             readoutString += to_string(amplitude) + " ";
    //         }
    //         readoutString += "]";
    //         IOUT("\t\treadout: " << readoutString);
    //     }
    // }

    return pulseVisualization;
}

Vec<QubitLines> generateQubitLines(const Vec<GateProperties> &gates,
                                   const PulseVisualization &pulseVisualization,
                                   const CircuitData &circuitData) {
    QL_DOUT("Generating qubit lines for pulse visualization...");

    // Find the gates per qubit.
    Vec<Vec<GateProperties>> gatesPerQubit(circuitData.amountOfQubits);
    for (const GateProperties &gate : gates) {
        for (const GateOperand &operand : getGateOperands(gate)) {
            if (operand.bitType == QUANTUM) {
                gatesPerQubit[operand.index].push_back(gate);
            }
        }
    }

    // Calculate the line segments for each qubit.
    Vec<QubitLines> linesPerQubit(circuitData.amountOfQubits);
    for (Int qubitIndex = 0; qubitIndex < circuitData.amountOfQubits; qubitIndex++) {
        // Find the cycles with pulses for each line.
        Line microwaveLine;
        Line fluxLine;
        Line readoutLine;

        for (const GateProperties &gate : gatesPerQubit[qubitIndex]) {
            const EndPoints gateCycles {gate.cycle, gate.cycle + (gate.duration / circuitData.cycleDuration) - 1};
            const Int codeword = gate.codewords[0];
            try {
                const GatePulses gatePulses = pulseVisualization.mapping.at(codeword).at(qubitIndex);

                if (!gatePulses.microwave.empty())
                    microwaveLine.segments.push_back({PULSE, gateCycles, {gatePulses.microwave, pulseVisualization.sampleRateMicrowave}});

                if (!gatePulses.flux.empty())
                    fluxLine.segments.push_back({PULSE, gateCycles, {gatePulses.flux, pulseVisualization.sampleRateFlux}});

                if (!gatePulses.readout.empty())
                    readoutLine.segments.push_back({PULSE, gateCycles, {gatePulses.readout, pulseVisualization.sampleRateReadout}});
            } catch (std::exception &e) {
                QL_WOUT("Missing codeword and/or qubit in waveform mapping file for gate: " << gate.name << "! Replacing pulse with flat line...\n\t" <<
                     "Indices are: codeword = " << codeword << " and qubit = " << qubitIndex << "\n\texception: " << e.what());
            }
        }

        microwaveLine.maxAmplitude = calculateMaxAmplitude(microwaveLine.segments);
        fluxLine.maxAmplitude = calculateMaxAmplitude(fluxLine.segments);
        readoutLine.maxAmplitude = calculateMaxAmplitude(readoutLine.segments);

        // Find the empty ranges between the existing segments and insert flat
        // segments there.
        insertFlatLineSegments(microwaveLine.segments, circuitData.getAmountOfCycles());
        insertFlatLineSegments(fluxLine.segments, circuitData.getAmountOfCycles());
        insertFlatLineSegments(readoutLine.segments, circuitData.getAmountOfCycles());

        // Construct the QubitLines object at the specified qubit index.
        linesPerQubit[qubitIndex] = { microwaveLine, fluxLine, readoutLine };

        // QL_DOUT("qubit: " << qubitIndex);
        // Str microwaveOutput = "\tmicrowave segments: ";
        // for (const LineSegment& segment : microwaveLineSegments)
        // {
        //     Str type;
        //     switch (segment.type)
        //     {
        //         case FLAT: type = "FLAT"; break;
        //         case PULSE: type = "PULSE"; break;
        //         case CUT: type = "CUT"; break;
        //     }
        //     microwaveOutput += " [" + type + " (" + to_string(segment.range.start) + "," + to_string(segment.range.end) + ")]";
        // }
        // QL_DOUT(microwaveOutput);

        // Str fluxOutput = "\tflux segments: ";
        // for (const LineSegment& segment : fluxLineSegments)
        // {
        //     Str type;
        //     switch (segment.type)
        //     {
        //         case FLAT: type = "FLAT"; break;
        //         case PULSE: type = "PULSE"; break;
        //         case CUT: type = "CUT"; break;
        //     }
        //     fluxOutput += " [" + type + " (" + to_string(segment.range.start) + "," + to_string(segment.range.end) + ")]";
        // }
        // QL_DOUT(fluxOutput);

        // Str readoutOutput = "\treadout segments: ";
        // for (const LineSegment& segment : readoutLineSegments)
        // {
        //     Str type;
        //     switch (segment.type)
        //     {
        //         case FLAT: type = "FLAT"; break;
        //         case PULSE: type = "PULSE"; break;
        //         case CUT: type = "CUT"; break;
        //     }
        //     readoutOutput += " [" + type + " (" + to_string(segment.range.start) + "," + to_string(segment.range.end) + ")]";
        // }
        // QL_DOUT(readoutOutput);
    }

    return linesPerQubit;
}

Real calculateMaxAmplitude(const Vec<LineSegment> &lineSegments) {
    Real maxAmplitude = 0;

    for (const LineSegment &segment : lineSegments) {
        const Vec<Real> waveform = segment.pulse.waveform;
        Real maxAmplitudeInSegment = 0;
        for (const Real amplitude : waveform) {
            const Real absAmplitude = abs(amplitude);
            if (absAmplitude > maxAmplitudeInSegment)
                maxAmplitudeInSegment = absAmplitude;
        }
        if (maxAmplitudeInSegment > maxAmplitude)
            maxAmplitude = maxAmplitudeInSegment;
    }

    return maxAmplitude;
}

void insertFlatLineSegments(Vec<LineSegment> &existingLineSegments, const Int amountOfCycles) {
    const Int minCycle = 0;
    const Int maxCycle = amountOfCycles - 1;
    for (Int i = minCycle; i <= maxCycle; i++) {
        for (Int j = i; j <= maxCycle; j++) {    
            if (j == maxCycle) {
                existingLineSegments.push_back( { FLAT, {i, j}, {{}, 0} } );
                i = maxCycle + 1;
                break;
            }

            Bool foundEndOfEmptyRange = false;
            for (const LineSegment &segment : existingLineSegments) {
                if (j == segment.range.start) {
                    foundEndOfEmptyRange = true;
                    // If the start of the new search for an empty range is also
                    // the start of a new non-empty range, skip adding a
                    // segment.
                    if (j != i) {
                        existingLineSegments.push_back( { FLAT, {i, j - 1}, {{}, 0} } );
                    }
                    i = segment.range.end;
                    break;
                }
            }

            if (foundEndOfEmptyRange) break;
        }
    }
}

void drawCycleLabels(cimg_library::CImg<Byte> &image,
                     const Layout &layout,
                     const CircuitData &circuitData,
                     const Structure &structure) {
    QL_DOUT("Drawing cycle labels...");

    for (Int i = 0; i < circuitData.getAmountOfCycles(); i++) {
        Str cycleLabel = "";
        Int cellWidth = 0;
        if (circuitData.isCycleCut(i)) {
            if (!circuitData.isCycleFirstInCutRange(i))
                continue;
            cellWidth = layout.cycles.cutting.getCutCycleWidth();
            cycleLabel = "...";
        } else {
            // cellWidth = structure.getCellDimensions().width;
            const Position4 cellPosition = structure.getCellPosition(i, 0, QUANTUM);
            cellWidth = cellPosition.x1 - cellPosition.x0;
            if (layout.cycles.labels.areInNanoSeconds()) {
                cycleLabel = to_string(i * circuitData.cycleDuration);
            } else {
                cycleLabel = to_string(i);
            }
        }

        Dimensions textDimensions = calculateTextDimensions(cycleLabel, layout.cycles.labels.getFontHeight());

        const Int xGap = (cellWidth - textDimensions.width) / 2;
        const Int yGap = (layout.cycles.labels.getRowHeight() - textDimensions.height) / 2;
        const Int xCycle = structure.getCellPosition(i, 0, QUANTUM).x0 + xGap;
        const Int yCycle = structure.getCycleLabelsY() + yGap;

        image.draw_text(xCycle, yCycle, cycleLabel.c_str(), layout.cycles.labels.getFontColor().data(), 0, 1, layout.cycles.labels.getFontHeight());
    }
}

void drawCycleEdges(cimg_library::CImg<Byte> &image,
                    const Layout &layout,
                    const CircuitData &circuitData,
                    const Structure &structure) {
    QL_DOUT("Drawing cycle edges...");

    for (Int i = 0; i < circuitData.getAmountOfCycles(); i++) {
        if (i == 0) continue;
        if (circuitData.isCycleCut(i) && circuitData.isCycleCut(i - 1)) continue;

        const Int xCycle = structure.getCellPosition(i, 0, QUANTUM).x0;
        const Int y0 = structure.getCircuitTopY();
        const Int y1 = structure.getCircuitBotY();

        image.draw_line(xCycle, y0, xCycle, y1, layout.cycles.edges.getColor().data(), layout.cycles.edges.getAlpha(), 0xF0F0F0F0);
    }
}

void drawBitLineLabels(cimg_library::CImg<Byte> &image,
                       const Layout &layout,
                       const CircuitData &circuitData,
                       const Structure &structure)
{
    QL_DOUT("Drawing bit line labels...");

    for (Int bitIndex = 0; bitIndex < circuitData.amountOfQubits; bitIndex++) {
        const Str label = "q" + to_string(bitIndex);
        const Dimensions textDimensions = calculateTextDimensions(label, layout.bitLines.labels.getFontHeight());

        const Int xGap = (structure.getCellDimensions().width - textDimensions.width) / 2;
        const Int yGap = (structure.getCellDimensions().height - textDimensions.height) / 2;
        const Int xLabel = structure.getBitLabelsX() + xGap;
        const Int yLabel = structure.getCellPosition(0, bitIndex, QUANTUM).y0 + yGap;

        image.draw_text(xLabel, yLabel, label.c_str(), layout.bitLines.labels.getQbitColor().data(), 0, 1, layout.bitLines.labels.getFontHeight());
    }

    if (layout.bitLines.classical.isEnabled()) {
        if (layout.bitLines.classical.isGrouped()) {
            const Str label = "C";
            const Dimensions textDimensions = calculateTextDimensions(label, layout.bitLines.labels.getFontHeight());

            const Int xGap = (structure.getCellDimensions().width - textDimensions.width) / 2;
            const Int yGap = (structure.getCellDimensions().height - textDimensions.height) / 2;
            const Int xLabel = structure.getBitLabelsX() + xGap;
            const Int yLabel = structure.getCellPosition(0, 0, CLASSICAL).y0 + yGap;

            image.draw_text(xLabel, yLabel, label.c_str(), layout.bitLines.labels.getCbitColor().data(), 0, 1, layout.bitLines.labels.getFontHeight());
        } else {
            for (Int bitIndex = 0; bitIndex < circuitData.amountOfClassicalBits; bitIndex++) {
                const Str label = "c" + to_string(bitIndex);
                const Dimensions textDimensions = calculateTextDimensions(label, layout.bitLines.labels.getFontHeight());

                const Int xGap = (structure.getCellDimensions().width - textDimensions.width) / 2;
                const Int yGap = (structure.getCellDimensions().height - textDimensions.height) / 2;
                const Int xLabel = structure.getBitLabelsX() + xGap;
                const Int yLabel = structure.getCellPosition(0, bitIndex, CLASSICAL).y0 + yGap;

                image.draw_text(xLabel, yLabel, label.c_str(), layout.bitLines.labels.getCbitColor().data(), 0, 1, layout.bitLines.labels.getFontHeight());
            }
        }
    }
}

void drawBitLineEdges(cimg_library::CImg<Byte> &image,
                      const Layout &layout,
                      const CircuitData &circuitData,
                      const Structure &structure)
{
    QL_DOUT("Drawing bit line edges...");

    const Int x0 = structure.getCellPosition(0, 0, QUANTUM).x0 - layout.grid.getBorderSize() / 2;
    const Int x1 = structure.getCellPosition(circuitData.getAmountOfCycles() - 1, 0, QUANTUM).x1 + layout.grid.getBorderSize() / 2;
    const Int yOffsetStart = -1 * layout.bitLines.edges.getThickness();

    for (Int bitIndex = 0; bitIndex < circuitData.amountOfQubits; bitIndex++) {
        if (bitIndex == 0) continue;

        const Int y = structure.getCellPosition(0, bitIndex, QUANTUM).y0;
        for (Int yOffset = yOffsetStart; yOffset < yOffsetStart + layout.bitLines.edges.getThickness(); yOffset++) {
            image.draw_line(x0, y + yOffset, x1, y + yOffset, layout.bitLines.edges.getColor().data(), layout.bitLines.edges.getAlpha());
        }
    }

    if (layout.bitLines.classical.isEnabled()) {
        if (layout.bitLines.classical.isGrouped()) {
            const Int y = structure.getCellPosition(0, 0, CLASSICAL).y0;
            for (Int yOffset = yOffsetStart; yOffset < yOffsetStart + layout.bitLines.edges.getThickness(); yOffset++) {
                image.draw_line(x0, y + yOffset, x1, y + yOffset, layout.bitLines.edges.getColor().data(), layout.bitLines.edges.getAlpha());
            }
        } else {
            for (Int bitIndex = 0; bitIndex < circuitData.amountOfClassicalBits; bitIndex++) {
                if (bitIndex == 0) continue;

                const Int y = structure.getCellPosition(0, bitIndex, CLASSICAL).y0;
                for (Int yOffset = yOffsetStart; yOffset < yOffsetStart + layout.bitLines.edges.getThickness(); yOffset++) {
                    image.draw_line(x0, y + yOffset, x1, y + yOffset, layout.bitLines.edges.getColor().data(), layout.bitLines.edges.getAlpha());
                }
            }
        }
    }
}

void drawBitLine(cimg_library::CImg<Byte> &image,
                 const Layout &layout,
                 const BitType bitType,
                 const Int row,
                 const CircuitData &circuitData,
                 const Structure &structure) {
    Color bitLineColor;
    Color bitLabelColor;
    switch (bitType) {
        case CLASSICAL:
            bitLineColor = layout.bitLines.classical.getColor();
            bitLabelColor = layout.bitLines.labels.getCbitColor();
            break;
        case QUANTUM:
            bitLineColor = layout.bitLines.quantum.getColor();
            bitLabelColor = layout.bitLines.labels.getQbitColor();
            break;
    }

    for (const Pair<EndPoints, Bool> &segment : structure.getBitLineSegments()) {
        const Int y = structure.getCellPosition(0, row, bitType).y0 + structure.getCellDimensions().height / 2;
        // Check if the segment is a cut segment.
        if (segment.second) {
            const Int height = structure.getCellDimensions().height / 8;
            const Int width = segment.first.end - segment.first.start;

            drawWiggle(image, segment.first.start, segment.first.end, y, width, height, bitLineColor);
        } else {
            image.draw_line(segment.first.start, y, segment.first.end, y, bitLineColor.data());
        }
    }
}

void drawGroupedClassicalBitLine(cimg_library::CImg<Byte> &image,
                                 const Layout &layout,
                                 const CircuitData &circuitData,
                                 const Structure &structure) {
    QL_DOUT("Drawing grouped classical bit lines...");

    const Int y = structure.getCellPosition(0, 0, CLASSICAL).y0 + structure.getCellDimensions().height / 2;

    // Draw the segments of the Real line.
    for (const Pair<EndPoints, Bool> &segment : structure.getBitLineSegments()) {
        // Check if the segment is a cut segment.
        if (segment.second) {
            const Int height = structure.getCellDimensions().height / 8;
            const Int width = segment.first.end - segment.first.start;
            
            drawWiggle(image, segment.first.start, segment.first.end, y - layout.bitLines.classical.getGroupedLineGap(),
                width, height, layout.bitLines.classical.getColor());           
            drawWiggle(image, segment.first.start, segment.first.end, y + layout.bitLines.classical.getGroupedLineGap(),
                width, height, layout.bitLines.classical.getColor());
        } else {
            image.draw_line(segment.first.start, y - layout.bitLines.classical.getGroupedLineGap(),
                segment.first.end, y - layout.bitLines.classical.getGroupedLineGap(), layout.bitLines.classical.getColor().data());
            image.draw_line(segment.first.start, y + layout.bitLines.classical.getGroupedLineGap(),
                segment.first.end, y + layout.bitLines.classical.getGroupedLineGap(), layout.bitLines.classical.getColor().data());
        }
    }

    // Draw the dashed line plus classical bit amount number on the first
    // segment.
    Pair<EndPoints, Bool> firstSegment = structure.getBitLineSegments()[0];
    //TODO: store the dashed line parameters in the layout object
    image.draw_line(firstSegment.first.start + 8, y + layout.bitLines.classical.getGroupedLineGap() + 2,
        firstSegment.first.start + 12, y - layout.bitLines.classical.getGroupedLineGap() - 3, layout.bitLines.classical.getColor().data());
    const Str label = to_string(circuitData.amountOfClassicalBits);
    //TODO: fix these hardcoded parameters
    const Int xLabel = firstSegment.first.start + 8;
    const Int yLabel = y - layout.bitLines.classical.getGroupedLineGap() - 3 - 13;
    image.draw_text(xLabel, yLabel, label.c_str(), layout.bitLines.labels.getCbitColor().data(), 0, 1, layout.bitLines.labels.getFontHeight());
}

void drawWiggle(cimg_library::CImg<Byte> &image,
                const Int x0,
                const Int x1,
                const Int y,
                const Int width,
                const Int height,
                const Color color) {
    image.draw_line(x0,    y, x0 + width / 3, y - height, color.data());
    image.draw_line(x0 + width / 3,        y - height,    x0 + width / 3 * 2,    y + height,    color.data());
    image.draw_line(x0 + width / 3 * 2,    y + height,    x1, y, color.data());
}

void drawLine(cimg_library::CImg<Byte> &image,
              const Structure &structure,
              const Int cycleDuration,
              const Line &line,
              const Int qubitIndex,
              const Int y,
              const Int maxLineHeight,
              const Color color) {
    for (const LineSegment &segment : line.segments) {
        const Int x0 = structure.getCellPosition(segment.range.start, qubitIndex, QUANTUM).x0;
        const Int x1 = structure.getCellPosition(segment.range.end, qubitIndex, QUANTUM).x1;
        const Int yMiddle = y + maxLineHeight / 2;

        switch (segment.type) {
            case FLAT: {
                image.draw_line(x0, yMiddle, x1, yMiddle, color.data());
            }
            break;

            case PULSE: {
                // Calculate pulse properties.
                QL_DOUT(" --- PULSE SEGMENT --- ");

                const Real maxAmplitude = line.maxAmplitude;

                const Int segmentWidth = x1 - x0; // pixels
                const Int segmentLengthInCycles = segment.range.end - segment.range.start + 1; // cycles
                const Int segmentLengthInNanoSeconds = cycleDuration * segmentLengthInCycles; // nanoseconds
                QL_DOUT("\tsegment width: " << segmentWidth);
                QL_DOUT("\tsegment length in cycles: " << segmentLengthInCycles);
                QL_DOUT("\tsegment length in nanoseconds: " << segmentLengthInNanoSeconds);

                const Int amountOfSamples = utoi(segment.pulse.waveform.size());
                const Int sampleRate = segment.pulse.sampleRate; // MHz
                const Real samplePeriod = 1000.0f * (1.0f / (Real) sampleRate); // nanoseconds
                const Int samplePeriodWidth = (Int) floor(samplePeriod / (Real) segmentLengthInNanoSeconds * (Real) segmentWidth); // pixels
                const Int waveformWidthInPixels = samplePeriodWidth * amountOfSamples;
                QL_DOUT("\tamount of samples: " << amountOfSamples);
                QL_DOUT("\tsample period in nanoseconds: " << samplePeriod);
                QL_DOUT("\tsample period width in segment: " << samplePeriodWidth);
                QL_DOUT("\ttotal waveform width in pixels: " << waveformWidthInPixels);

                if (waveformWidthInPixels > segmentWidth) {
                    QL_WOUT("The waveform duration in cycles " << segment.range.start << " to " << segment.range.end << " on qubit " << qubitIndex <<
                         " seems to be larger than the duration of those cycles. Please check the sample rate and amount of samples.");
                }

                // Calculate sample positions.
                const Real amplitudeUnitHeight = (Real) maxLineHeight / (maxAmplitude * 2.0f);
                Vec<Position2> samplePositions;
                for (UInt i = 0; i < segment.pulse.waveform.size(); i++) {
                    const Int xSample = x0 + utoi(i) * samplePeriodWidth;

                    const Real amplitude = segment.pulse.waveform[i];
                    const Real adjustedAmplitude = amplitude + maxAmplitude;
                    const Int ySample = max(y, y + maxLineHeight - 1 - (Int) floor(adjustedAmplitude * amplitudeUnitHeight));

                    samplePositions.push_back( {xSample, ySample} );
                }

                // Draw the lines connecting the samples.
                for (UInt i = 0; i < samplePositions.size() - 1; i++) {
                    const Position2 currentSample = samplePositions[i];
                    const Position2 nextSample = samplePositions[i + 1];

                    image.draw_line(currentSample.x, currentSample.y, nextSample.x, nextSample.y, color.data());
                }
                // Draw line from last sample to next segment.
                const Position2 lastSample = samplePositions[samplePositions.size() - 1];
                image.draw_line(lastSample.x, lastSample.y, x1, yMiddle, color.data());
            }
            break;

            case CUT: {
                // drawWiggle(image,
                //     cellPosition.x0,
                //     cellPosition.x1,
                //     cellPosition.y0 + layout.pulses.pulseRowHeightMicrowave / 2,
                //     cellPosition.x1 - cellPosition.x0,
                //     layout.pulses.pulseRowHeightMicrowave / 8,
                //     layout.pulses.pulseColorMicrowave);
            }
            break;
        }
    }
}

void drawCycle(cimg_library::CImg<Byte> &image,
               const Layout &layout,
               const CircuitData &circuitData,
               const Structure &structure,
               const Cycle &cycle)
{
    // Draw each of the chunks in the cycle's gate partition.
    for (UInt chunkIndex = 0; chunkIndex < cycle.gates.size(); chunkIndex++)
    {
        const Int chunkOffset = utoi(chunkIndex) * structure.getCellDimensions().width;

        // Draw each of the gates in the current chunk.
        for (const GateProperties &gate : cycle.gates[chunkIndex])
        {
            drawGate(image, layout, circuitData, gate, structure, chunkOffset);
        }
    }
}

void drawGate(cimg_library::CImg<Byte> &image,
              const Layout &layout,
              const CircuitData &circuitData,
              const GateProperties &gate,
              const Structure &structure,
              const Int chunkOffset) {
    // Get the gate visualization parameters.
    GateVisual gateVisual;
    if (gate.type == __custom_gate__) {
        if (layout.customGateVisuals.count(gate.visual_type) == 1) {
            QL_DOUT("Found visual for custom gate: '" << gate.name << "'");
            gateVisual = layout.customGateVisuals.at(gate.visual_type);
        } else {
            // TODO: try to recover by matching gate name with a default visual name
            // TODO: if the above fails, display a dummy gate
            QL_WOUT("Did not find visual for custom gate: '" << gate.name << "', skipping gate!");
            return;
        }
    } else {
        QL_DOUT("Default gate found. Using default visualization!");
        gateVisual = layout.defaultGateVisuals.at(gate.type);
    }

    // Fetch the operands used by this gate.
    QL_DOUT(gate.name);
    Vec<GateOperand> operands = getGateOperands(gate);
    for (const GateOperand &operand : operands) {
        QL_DOUT("bitType: " << operand.bitType << " value: " << operand.index);
    }

    // Check for correct amount of nodes.
    if (operands.size() != gateVisual.nodes.size()) {
        QL_WOUT("Amount of gate operands: " << operands.size() << " and visualization nodes: " << gateVisual.nodes.size()
             << " are not equal. Skipping gate with name: '" << gate.name << "' ...");
        return;
    }

    if (operands.size() > 1) {
        // Draw the lines between each node. If this is done before drawing the
        // nodes, there is no need to calculate line segments, we can just draw 
        // one big line between the nodes and the nodes will be drawn on top of
        // those.

        QL_DOUT("Setting up multi-operand gate...");
        Pair<GateOperand, GateOperand> edgeOperands = calculateEdgeOperands(operands, circuitData.amountOfQubits);
        GateOperand minOperand = edgeOperands.first;
        GateOperand maxOperand = edgeOperands.second;

        const Int column = gate.cycle;
        QL_DOUT("minOperand.bitType: " << minOperand.bitType << " minOperand.operand " << minOperand.index);
        QL_DOUT("maxOperand.bitType: " << maxOperand.bitType << " maxOperand.operand " << maxOperand.index);
        QL_DOUT("cycle: " << column);

        const Position4 topCellPosition = structure.getCellPosition(column, minOperand.index, minOperand.bitType);
        const Position4 bottomCellPosition = structure.getCellPosition(column, maxOperand.index, maxOperand.bitType);
        const Position4 connectionPosition = {
            topCellPosition.x0 + chunkOffset + structure.getCellDimensions().width / 2,
            topCellPosition.y0 + structure.getCellDimensions().height / 2,
            bottomCellPosition.x0 + chunkOffset + structure.getCellDimensions().width / 2,
            bottomCellPosition.y0 + structure.getCellDimensions().height / 2,
        };

        //TODO: probably have connection line type as part of a gate's visual definition
        if (isMeasurement(gate)) {
            if (layout.measurements.isConnectionEnabled() && layout.bitLines.classical.isEnabled()) {
                const Int groupedClassicalLineOffset = layout.bitLines.classical.isGrouped() ? layout.bitLines.classical.getGroupedLineGap() : 0;

                image.draw_line(connectionPosition.x0 - layout.measurements.getLineSpacing(),
                    connectionPosition.y0,
                    connectionPosition.x1 - layout.measurements.getLineSpacing(),
                    connectionPosition.y1 - layout.measurements.getArrowSize() - groupedClassicalLineOffset,
                    gateVisual.connectionColor.data());

                image.draw_line(connectionPosition.x0 + layout.measurements.getLineSpacing(),
                    connectionPosition.y0,
                    connectionPosition.x1 + layout.measurements.getLineSpacing(),
                    connectionPosition.y1 - layout.measurements.getArrowSize() - groupedClassicalLineOffset,
                    gateVisual.connectionColor.data());

                const Int x0 = connectionPosition.x1 - layout.measurements.getArrowSize() / 2;
                const Int y0 = connectionPosition.y1 - layout.measurements.getArrowSize() - groupedClassicalLineOffset;
                const Int x1 = connectionPosition.x1 + layout.measurements.getArrowSize() / 2;
                const Int y1 = connectionPosition.y1 - layout.measurements.getArrowSize() - groupedClassicalLineOffset;
                const Int x2 = connectionPosition.x1;
                const Int y2 = connectionPosition.y1 - groupedClassicalLineOffset;
                image.draw_triangle(x0, y0, x1, y1, x2, y2, gateVisual.connectionColor.data(), 1);
            }
        } else {
            image.draw_line(connectionPosition.x0, connectionPosition.y0, connectionPosition.x1, connectionPosition.y1, gateVisual.connectionColor.data());
        }
        QL_DOUT("Finished setting up multi-operand gate");
    }

    // Draw the gate duration outline if the option has been set.
    if (!layout.cycles.areCompressed() && layout.gateDurationOutlines.areEnabled()) {
        QL_DOUT("Drawing gate duration outline...");
        const Int gateDurationInCycles = gate.duration / circuitData.cycleDuration;
        // Only draw the gate outline if the gate takes more than one cycle.
        if (gateDurationInCycles > 1) {
            for (UInt i = 0; i < operands.size(); i++) {
                const Int columnStart = gate.cycle;
                const Int columnEnd = columnStart + gateDurationInCycles - 1;
                const Int row = (i >= gate.operands.size()) ? gate.creg_operands[i - gate.operands.size()] : gate.operands[i];
                QL_DOUT("i: " << i << " size: " << gate.operands.size() << " value: " << gate.operands[i]);

                const Int x0 = structure.getCellPosition(columnStart, row, QUANTUM).x0 + chunkOffset + layout.gateDurationOutlines.getGap();
                const Int y0 = structure.getCellPosition(columnStart, row, QUANTUM).y0 + layout.gateDurationOutlines.getGap();
                const Int x1 = structure.getCellPosition(columnEnd, row, QUANTUM).x1 - layout.gateDurationOutlines.getGap();
                const Int y1 = structure.getCellPosition(columnEnd, row, QUANTUM).y1 - layout.gateDurationOutlines.getGap();

                // Draw the outline in the colors of the node.
                const Node node = gateVisual.nodes.at(i);
                image.draw_rectangle(x0, y0, x1, y1, node.backgroundColor.data(), layout.gateDurationOutlines.getFillAlpha());
                image.draw_rectangle(x0, y0, x1, y1, node.outlineColor.data(), layout.gateDurationOutlines.getOutlineAlpha(), 0xF0F0F0F0);
                
                //image.draw_rectangle(x0, y0, x1, y1, layout.cycles.gateDurationOutlineColor.data(), layout.cycles.gateDurationAlpha);
                //image.draw_rectangle(x0, y0, x1, y1, layout.cycles.gateDurationOutlineColor.data(), layout.cycles.gateDurationOutLineAlpha, 0xF0F0F0F0);
            }
        }
    }

    // Draw the nodes.
    QL_DOUT("Drawing gate nodes...");
    for (UInt i = 0; i < operands.size(); i++) {
        QL_DOUT("Drawing gate node with index: " << i << "...");
        //TODO: change the try-catch later on! the gate config will be read from somewhere else than the default layout
        try {
            const Node node = gateVisual.nodes.at(i);
            const BitType operandType = (i >= gate.operands.size()) ? CLASSICAL : QUANTUM;
            const Int index = utoi((operandType == QUANTUM) ? i : (i - gate.operands.size()));

            const Cell cell = {
                gate.cycle,
                operandType == CLASSICAL ? gate.creg_operands.at(index) + circuitData.amountOfQubits : gate.operands.at(index),
                chunkOffset,
                operandType
            };

            switch (node.type) {
                case NONE:        QL_DOUT("node.type = NONE"); break; // Do nothing.
                case GATE:        QL_DOUT("node.type = GATE"); drawGateNode(image, layout, structure, node, cell); break;
                case CONTROL:    QL_DOUT("node.type = CONTROL"); drawControlNode(image, layout, structure, node, cell); break;
                case NOT:        QL_DOUT("node.type = NOT"); drawNotNode(image, layout, structure, node, cell); break;
                case CROSS:        QL_DOUT("node.type = CROSS"); drawCrossNode(image, layout, structure, node, cell); break;
                default:        QL_WOUT("Unknown gate display node type!"); break;
            }
        } catch (const std::out_of_range &e) {
            QL_WOUT(Str(e.what()));
            return;
        }
        
        QL_DOUT("Finished drawing gate node with index: " << i << "...");
    }
}

void drawGateNode(cimg_library::CImg<Byte> &image,
                  const Layout &layout,
                  const Structure &structure,
                  const Node &node,
                  const Cell &cell) {
    const Int xGap = (structure.getCellDimensions().width - node.radius * 2) / 2;
    const Int yGap = (structure.getCellDimensions().height - node.radius * 2) / 2;

    const Position4 cellPosition = structure.getCellPosition(cell.col, cell.row, cell.bitType);
    const Position4 position = {
        cellPosition.x0 + cell.chunkOffset + xGap,
        cellPosition.y0 + yGap,
        cellPosition.x0 + cell.chunkOffset + structure.getCellDimensions().width - xGap,
        cellPosition.y1 - yGap
    };

    // Draw the gate background.
    image.draw_rectangle(position.x0, position.y0, position.x1, position.y1, node.backgroundColor.data());
    image.draw_rectangle(position.x0, position.y0, position.x1, position.y1, node.outlineColor.data(), 1, 0xFFFFFFFF);

    // Draw the gate symbol. The width and height of the symbol are calculated first to correctly position the symbol within the gate.
    Dimensions textDimensions = calculateTextDimensions(node.displayName, node.fontHeight);
    image.draw_text(position.x0 + (node.radius * 2 - textDimensions.width) / 2, position.y0 + (node.radius * 2 - textDimensions.height) / 2,
        node.displayName.c_str(), node.fontColor.data(), 0, 1, node.fontHeight);
}

void drawControlNode(cimg_library::CImg<Byte> &image,
                     const Layout &layout,
                     const Structure &structure,
                     const Node &node,
                     const Cell &cell) {
    const Position4 cellPosition = structure.getCellPosition(cell.col, cell.row, cell.bitType);
    const Position2 position = {
        cellPosition.x0 + cell.chunkOffset + structure.getCellDimensions().width / 2,
        cellPosition.y0 + cell.chunkOffset + structure.getCellDimensions().height / 2
    };

    image.draw_circle(position.x, position.y, node.radius, node.backgroundColor.data());
}

void drawNotNode(cimg_library::CImg<Byte> &image,
                 const Layout &layout,
                 const Structure &structure,
                 const Node &node,
                 const Cell &cell) {
    // TODO: allow for filled not node instead of only an outline not node

    const Position4 cellPosition = structure.getCellPosition(cell.col, cell.row, cell.bitType);
    const Position2 position = {
        cellPosition.x0 + cell.chunkOffset + structure.getCellDimensions().width / 2,
        cellPosition.y0 + cell.chunkOffset + structure.getCellDimensions().height / 2
    };

    // Draw the outlined circle.
    image.draw_circle(position.x, position.y, node.radius, node.backgroundColor.data(), 1, 0xFFFFFFFF);

    // Draw two lines to represent the plus sign.
    const Int xHor0 = position.x - node.radius;
    const Int xHor1 = position.x + node.radius;
    const Int yHor = position.y;

    const Int xVer = position.x;
    const Int yVer0 = position.y - node.radius;
    const Int yVer1 = position.y + node.radius;

    image.draw_line(xHor0, yHor, xHor1, yHor, node.backgroundColor.data());
    image.draw_line(xVer, yVer0, xVer, yVer1, node.backgroundColor.data());
}

void drawCrossNode(cimg_library::CImg<Byte> &image,
                   const Layout &layout,
                   const Structure &structure,
                   const Node &node,
                   const Cell &cell) {
    const Position4 cellPosition = structure.getCellPosition(cell.col, cell.row, cell.bitType);
    const Position2 position = {
        cellPosition.x0 + cell.chunkOffset + structure.getCellDimensions().width / 2,
        cellPosition.y0 + cell.chunkOffset + structure.getCellDimensions().height / 2
    };

    // Draw two diagonal lines to represent the cross.
    const Int x0 = position.x - node.radius;
    const Int y0 = position.y - node.radius;
    const Int x1 = position.x + node.radius;
    const Int y1 = position.y + node.radius;

    image.draw_line(x0, y0, x1, y1, node.backgroundColor.data());
    image.draw_line(x0, y1, x1, y0, node.backgroundColor.data());
}

} // namespace ql

#endif //WITH_VISUALIZER