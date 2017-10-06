/**
 * @file   utils.h
 * @date   04/2017
 * @author Nader Khammassi
 * @brief  string utils (from qx)
 */

#ifndef QL_UTILS_H
#define QL_UTILS_H

#include "str.h"

#include <limits>

#define println(x) std::cout << "[OPENQL] "<< x << std::endl

#define COUT(content) std::cout << "[OPENQL] " << __FILE__ <<":"<< __LINE__ <<" "<< content << std::endl
#define WOUT(content) std::cout << "[OPENQL] " << __FILE__ <<":"<< __LINE__ <<" Warning: "<< content << std::endl
#define EOUT(content) std::cout << "[OPENQL] " << __FILE__ <<":"<< __LINE__ <<" Error: "<< content << std::endl

// #ifdef DEBUG
// #define DOUT(content)                          COUT(content)
// #else
// #define DOUT(content)
// #endif

#define DOUT(content)


auto MAX_CYCLE = std::numeric_limits<int>::max(); // TODO should go to utils

namespace ql
{
    /**
     * utils
     */
    namespace utils
    {
        std::string output_dir("test_output");
        void set_output_dir(std::string dir)
        {
            output_dir = dir;
        }
        std::string get_output_dir()
        {
            return output_dir;
        }

        /**
         * @param str
         *    string to be processed
         * @param seq
         *    string to be replaced
         * @param rep
         *    string used to replace seq
         * @brief
         *    replace recursively seq by rep in str
         */
        void replace_all(std::string &str, std::string seq, std::string rep)
        {
	   str::replace_all(str,seq,rep);
        }

        /**
         * string starts with " and end with "
         * return the content of the string between the commas
         */
        bool format_string(std::string& s)
        {
            replace_all(s,"\\n","\n");
            size_t pf = s.find("\"");
            if (pf == std::string::npos)
                return false;
            size_t ps = s.find_last_of("\"");
            if (ps == std::string::npos)
                return false;
            if (ps == pf)
                return false;
            s = s.substr(pf+1,ps-pf-1);
            return true;
        }

	/**
	 * write content to the file <file_name>
	 */
        void write_file(std::string file_name, std::string& content)
        {
            std::ofstream file;
            file.open(file_name);
            if ( file.fail() )
            {
                std::cout << "[x] error opening file '" << file_name << "' !" << std::endl
                          << "         make sure the output directory exists for '" << file_name << "'" << std::endl;
                return;
            }

            file << content;
            file.close();
        }


	/**
	 * print vector
	 */
	 template<typename T>
	 void print_vector(std::vector<T> v, std::string prefix="", std::string separator=" | ")
	 {
	    std::cout << prefix << " [";
	    size_t sz = v.size()-1;
	    for (size_t i=0; i<sz; ++i)
	       std::cout << v[i] << separator; 
	    std::cout << v[sz] << "]" << std::endl;
	 }


    } // utils
} // ql

#endif //QL_UTILS_H

