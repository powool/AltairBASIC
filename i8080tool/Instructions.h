#pragma once

#include <string>
#include <cstdint>
#include <map>

#include "SymbolTable.hpp"

#define DATA_BYTE 0x100
#define DATA_WORD 0x101
#define DATA_TEXT 0x102
#define DATA_RET 0x103

enum instruction_type {
	CONTROL, BRANCH, ARITHMETIC, MOVE, DATA
};

//IMMEDIATE HYBRID: Classified as an immediate value but frequently used as an address (eg. LXI instructions)
enum operand_type {
	NONE, IMMEDIATE, ADDRESS, IMMEDIATE_HYBRID, CHARACTER
};

struct Instruction {
	const int opcode;
	const std::string mnemonic;
	const int instruction_type;
	const int operand_length;
	const int operand_type;

	Instruction(int opcode, std::string mnemonic,int instruction_type, int operand_length, int operand_type) :
		opcode(opcode),
		mnemonic(mnemonic),
		instruction_type(instruction_type),
		operand_length(operand_length),
		operand_type(operand_type) {}
	int GetLength(uint16_t address, SymbolTable &symbolTable) const;
	std::string AsString(
		uint16_t address,
		uint8_t op1,
		uint8_t op2,
		SymbolTable &symbols,
		bool setSymbol) const;
};

const extern Instruction instructions8085[256];
