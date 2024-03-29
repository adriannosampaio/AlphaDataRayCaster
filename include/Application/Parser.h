#ifndef _PARSER_H_
#define _PARSER_H_

#include <memory>




namespace Application
{
    

    /** Initialize the CLI argument parser from
    *   SimpleArgParser lib. 
    *
    *   @param argc
    *       Number of command line arguments. Redirected from main
    *   @param argv
    *       Array of CLI string arguments. Redirected from main
    */
    void InitParser(int argc, char** argv)
    {
        // Init parser
        argumentParser = 
            make_shared<parser::ArgumentParser>(argc, argv);

        // Add input file argument
        argumentParser->addArgument(
            arguments::file,
            true,
            "Path to a .dark input file"
        );

        // Add the output image file argument
        argumentParser->addArgument(
            arguments::output,
            true,
            "Path to a PPM output file."
        );

        argumentParser->addArgument(
            arguments::use_fpga,
            false,
            "Render the image using the FPGA board."
        );
        argumentParser->parse();

    }
}

#endif