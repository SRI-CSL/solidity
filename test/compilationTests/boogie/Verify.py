#!/usr/bin/env python3

import argparse
import re
import subprocess
import os

solc = "~/Workspace/solidity-sri/build/solc/solc"
boogie = "~/Workspace/boogie/Binaries/Boogie.exe"

def main():
    global solc, boogie

    # Set up argument parser
    parser = argparse.ArgumentParser(description='Verify Solidity smart contracts.')
    parser.add_argument('file', metavar='f', type=str, help='Path to the input file')
    parser.add_argument('--bit-precise', action='store_true', help='Enable bit-precise verification')
    parser.add_argument("--solc", type=str, help="Solidity compiler to use (with boogie translator)")
    parser.add_argument("--boogie", type=str, help="Boogie binary to use")
    args = parser.parse_args()

    if args.solc is not None:
        solc = args.solc
    if args.boogie is not None:
        boogie = args.boogie

    solFile = args.file
    bplFile = os.path.basename(solFile) + ".bpl"

    # First convert .sol to .bpl
    solcArgs = " --boogie " + solFile + " -o ./ --overwrite" + (" --bit-precise" if args.bit_precise else "")
    convertCommand = solc + " " + solcArgs
    try:
        subprocess.check_output(convertCommand, shell = True, stderr=subprocess.STDOUT)
    except subprocess.CalledProcessError as err:
        compilerOutputStr = err.output.decode("utf-8")
        print(compilerOutputStr)
        return

    # Run verification, get result
    boogieArgs = "/nologo /doModSetAnalysis /errorTrace:0"
    verifyCommand = "mono " + boogie + " " + bplFile + " " + boogieArgs
    verifierOutput = subprocess.check_output(verifyCommand, shell = True)
    verifierOutputStr = verifierOutput.decode("utf-8")
    if re.search("Boogie program verifier finished with", verifierOutputStr) == None:
        print("Error(s) while running verifier, details:")
        print(verifierOutputStr)
        return

    # Map results back to .sol file
    outputLines = list(filter(None, verifierOutputStr.split("\n")))
    for outputLine, nextOutputLine in zip(outputLines, outputLines[1:]):
        if "This assertion might not hold." in outputLine:
            errLine = getRelatedLineFromBpl(outputLine, 0) # Info is in the current line
            print(getSourceLineAndCol(errLine) + ": " + getMessage(errLine))
        if "A postcondition might not hold on this return path." in outputLine:
            errLine = getRelatedLineFromBpl(nextOutputLine, 0) # Info is in the next line
            print(getSourceLineAndCol(errLine) + ": " + getMessage(errLine))
        if "A precondition for this call might not hold." in outputLine:
            errLine = getRelatedLineFromBpl(nextOutputLine, 0) # Message is in the next line
            errLinePrev = getRelatedLineFromBpl(outputLine, -1) # Location is in the line before
            print(getSourceLineAndCol(errLinePrev) + ": " + getMessage(errLine))

    if (re.match("Boogie program verifier finished with \\d+ verified, 0 errors", outputLines[0])):
        print("No errors found.")

# Gets the line related to an error in the output
def getRelatedLineFromBpl(outputLine, offset):
    errFileLineCol = outputLine.split(":")[0]
    errFile = errFileLineCol[:errFileLineCol.rfind("(")]
    errLineNo = int(errFileLineCol[errFileLineCol.rfind("(")+1:errFileLineCol.rfind(",")]) - 1
    return open(errFile).readlines()[errLineNo + offset]

# Gets the original (.sol) line and column number from an annotated line in the .bpl
def getSourceLineAndCol(line):
    match = re.search("{:sourceloc \"([^}]*)\", (\\d+), (\\d+)}", line)
    return match.group(1) + ", line " + match.group(2) + ", col " + match.group(3)

# Gets the message from an annotated line in the .bpl
def getMessage(line):
    return re.search("{:message \"([^}]*)\"}", line).group(1)

if __name__== "__main__":
    main()