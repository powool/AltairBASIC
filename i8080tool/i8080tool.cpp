#include <cctype>
#include <chrono>
#include <format>
#include <fstream>
#include <filesystem>
#include <iostream>
#include <cmath>
#include <sstream>
#include <thread>
#include <utility>
#include <vector>

#include <getopt.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <time.h>

#include "i8080.h"
#include "i8080_hal.h"
#include "Instructions.h"
#include "SymbolTable.hpp"

SymbolTable symbolTable;

// see https://altairclone.com/downloads/basic/Paper%20Tape%20and%20Cassette/4K%20BASIC%20Program%20Load%20and%20Save/MBLESSM.ASM
uint16_t LoadAltairFile(const std::string &filename, uint8_t *memory)
{
	std::ifstream in(filename, std::ios::binary);

	if (!in.good()) {
		std::cerr << std::format("Failed to open file {}\n", filename);
		exit(1);
	}

	int loadSize = 0;
	uint8_t ch;
	uint8_t tmpChar;
	auto getc = [&in] (uint8_t &ch) {in.read(reinterpret_cast<char*>(&ch),1);};
	getc(ch);
	while (ch != 0) {
		getc(tmpChar);
		if (tmpChar != ch) { break; }
		ch = tmpChar;
	}

	// skip loader
	while(ch-- > 0) {
		getc(tmpChar);
	}

	uint8_t length;
	uint16_t address;
	uint16_t firstAddress = 0xffff;
	uint16_t lastAddress = 0x0;
	uint8_t checksum;
	uint8_t checksum_check;
	// find first 0x3c after program loader
	while (!in.eof()) {
		getc(ch);
		switch(ch) {
			case 0x3c:	// Altair paper tape data record
				getc(length);

				getc(ch);
				checksum = ch;
				address = ch;

				getc(ch);
				checksum += ch;
				address |= ch << 8;

				firstAddress = std::min(firstAddress, address);

				while(!in.eof() && length--) {
					getc(ch);
					memory[address] = ch;
					checksum += memory[address];
					loadSize++;
					address++;
				}
				lastAddress = std::max(lastAddress, address);
				getc(checksum_check);
				if(checksum != checksum_check) {
					fprintf(stderr,"bad checksum %02x - fix\n", checksum);
				}
				break;
			case 0x78:	// Altair paper tape execute address record
				getc(ch);
				address = ch;
				getc(ch);
				address = ch << 8;
				std::cerr << "\n*********************************\n";
				std::cerr << std::format("File \"{}\" loaded, size {}\n", filename.c_str(), loadSize);
				std::cerr << std::format("Address range {:04x}:{:04X}\n", firstAddress, lastAddress);
				return address;
			default:
				break;
		}
	}
	return 0;
}

void LoadFile(const std::string &filename, uint8_t *memory, int offset)
{
	std::ifstream in(filename, std::ios::binary);

	if (!in.good()) {
		std::cerr << std::format("Failed to open file {}\n", filename);
		exit(1);
	}

	int loadSize = 0;
	auto memoryStart = memory;
	while (!in.eof()) {
		uint8_t ch;
		in.read(reinterpret_cast<char *>(&ch), 1);
		*(offset+memory++) = ch;
		loadSize += 1;
	}
	std::cerr << "\n*********************************\n";
	std::cerr << std::format("File \"{}\" loaded, size {}\n", filename, loadSize);
	std::cerr << std::format("Address range {:04X}:{:04X}\n", offset, offset + memory - memoryStart);
}

template <typename T> int ArithmeticSign(T val) {
    return (T(0) < val) - (val < T(0));
}

struct Single {
	Single(uint8_t m0, uint8_t m1, uint8_t m2, uint8_t exp) : m0(m0), m1(m1), m2(m2), exp(exp) {;}
	Single() {;}
	uint8_t m0 = 0;
	uint8_t m1 = 0;
	uint8_t m2 = 0;
	uint8_t exp = 0;
	double Mantissa() {
		return 1 + (double) ((int(m2) & 0x7f) << 16 | int(m1) << 8 | int(m0)) / (1 << 23);
	}
	int Sign() {
		return (((m2 & 0x80)) != 0 ) ? -1 : 1;
	}
	int Exponent() {
		return int(exp) - 128;
	}
	double Value() {
		std::cerr << std::format(" SNG: {:02X} {:02X} {:02X} {:02X} ", exp, m2, m1, m0);
		double magnitude = Exponent() == 0 ? 1.0 : pow(2, Exponent()) / 2.0;
		return (exp != 0) ? (Sign() * Mantissa() * magnitude) : 0;
	}
	void Load(uint16_t addr) {
		m0 = i8080_hal_memory_read_byte(addr);
		m1 = i8080_hal_memory_read_byte(addr+1);
		m2 = i8080_hal_memory_read_byte(addr+2);
		exp = i8080_hal_memory_read_byte(addr+3);
	}
};

struct Double {
	Double(uint8_t m0, uint8_t m1, uint8_t m2, uint8_t m3,
			uint8_t m4, uint8_t m5, uint8_t m6, uint8_t exp) :
		m0(m0), m1(m1), m2(m2), m3(m3), m4(m4), m5(m5), m6(m6), exp(exp) {;}
	Double() {;}
	uint8_t m0 = 0;
	uint8_t m1 = 0;
	uint8_t m2 = 0;
	uint8_t m3 = 0;
	uint8_t m4 = 0;
	uint8_t m5 = 0;
	uint8_t m6 = 0;
	uint8_t exp = 0;
	double Mantissa() {
		int64_t m = ((static_cast<int64_t>(m6) & 0x7f) << 48) |
			    (static_cast<int64_t>(m5) << 40) |
			    (static_cast<int64_t>(m4) << 32) |
			    (static_cast<int64_t>(m3) << 24) |
			    (static_cast<int64_t>(m2) << 16) |
			    (static_cast<int64_t>(m1) << 8 |
			    (static_cast<int64_t>(m0)));
		return 1 + static_cast<double>(m) / (static_cast<int64_t>(1) << 55) ;
	}
	int Sign() {
		return (((m6 & 0x80)) != 0 ) ? -1 : 1;
	}
	int Exponent() {
		return int(exp) - 128;
	}
	double Value() {
		std::cout << std::format(" DBL: {:02X} {:02X} {:02X} {:02X} ", exp, m6, m5, m4);
		double magnitude = Exponent() == 0 ? 1.0 : pow(2, Exponent()) / 2.0;
		return (exp != 0) ? (Sign() * Mantissa() * magnitude) : 0;
	}
	void Load(uint16_t addr) {
		m0 = i8080_hal_memory_read_byte(addr);
		m1 = i8080_hal_memory_read_byte(addr+1);
		m2 = i8080_hal_memory_read_byte(addr+2);
		m3 = i8080_hal_memory_read_byte(addr+3);
		m4 = i8080_hal_memory_read_byte(addr+4);
		m5 = i8080_hal_memory_read_byte(addr+5);
		m6 = i8080_hal_memory_read_byte(addr+6);
		exp = i8080_hal_memory_read_byte(addr+7);
	}
};

std::string GetFAC()
{
	static uint16_t facAddr = 0x0;
	static uint16_t facLoAddr = 0x0;
	static uint16_t dFacLoAddr = 0x0;
	static uint16_t valTypAddr = 0x0;
	static uint16_t argLoAddr = 0x0;

	if (argLoAddr == 0x0) {
		auto symbol = symbolTable.GetSymbol("ARGLO");
		if(symbol) {
			argLoAddr = symbol->value;
		} else {
			argLoAddr = 0x01;
		}
	}

	if (facAddr == 0x0) {
		auto symbol = symbolTable.GetSymbol("FAC");
		if(symbol) {
			facAddr = symbol->value;
		} else {
			facAddr = 0x01;
		}
	}

	if (facLoAddr == 0x0) {
		auto symbol = symbolTable.GetSymbol("FACLO");
		if(symbol) {
			facLoAddr = symbol->value;
		} else {
			facLoAddr = 0x01;
		}
	}

	if (dFacLoAddr == 0x0) {
		auto symbol = symbolTable.GetSymbol("DFACLO");
		if(symbol) {
			dFacLoAddr = symbol->value;
		} else {
			dFacLoAddr = 0x01;
		}
	}

	if (valTypAddr == 0x0) {
		auto symbol = symbolTable.GetSymbol("VALTYP");
		if(symbol) {
			valTypAddr = symbol->value;
		} else {
			valTypAddr = 0x01;
		}
	}

	if (facAddr < 0x02 || facLoAddr < 0x02 || valTypAddr < 0x02) {
		return "";
	}

	std::string valType;
	switch (i8080_hal_memory_read_byte(valTypAddr)) {
		// 16 bit signed integer
		case 2:
			valType = std::format("integer ({})", i8080_hal_memory_read_word(facLoAddr));
			break;
		// string:
		case 3: {
				uint16_t descriptorPtr = i8080_hal_memory_read_word(facLoAddr);
				uint8_t length = i8080_hal_memory_read_byte(descriptorPtr);
				uint16_t stringPtr = i8080_hal_memory_read_word(descriptorPtr + 1);
				std::string result;
				for(int i = 0; i < length ; i++) {
					char byte = i8080_hal_memory_read_byte(stringPtr + i);
					if (std::isprint(byte)) {
						result.push_back(byte);
					} else {
						result.push_back('.');
					}
				}
				valType = std::format("string ({:02X}: {}", length, result);
			}
			break;
		// single precision FP
		case 4: {
				Single f;
				f.Load(facLoAddr);
				valType = std::format("float {}", f.Value());
			}
			break;
		// double precision FP
		case 8: {
				Double f;
				f.Load(dFacLoAddr);
				valType = std::format("double {}", f.Value());
				Double a;
				a.Load(argLoAddr);
				valType += std::format(" arg double {}", a.Value());
			}
			break;
		default:
			valType = std::format("unknown ({:02X})", i8080_hal_memory_read_byte(valTypAddr));
			break;
	}
	return valType;
}

void DumpState(i8080_state *cpu, uint8_t *memory, int instructionCount, bool setSymbol)
{

	uint16_t pc = i8080_pc(cpu);
	uint8_t a = i8080_regs_a(cpu);
	uint16_t bc = i8080_regs_bc(cpu);
	uint16_t de = i8080_regs_de(cpu);
	uint16_t hl = i8080_regs_hl(cpu);
	uint16_t sp = i8080_regs_sp(cpu);
	uint8_t m = memory[hl];
	uint16_t starsp = memory[sp] | memory[sp+1] << 8;
	uint8_t starpc = memory[pc];
	uint8_t starpc1 = memory[pc+1];
	uint8_t starpc2 = memory[pc+2];
	auto disassembly = instructions8085[starpc].AsString(
		pc,
		starpc1,
		starpc2,
		symbolTable,
		setSymbol);
	std::cerr << std::format("{:08d}", instructionCount);
//	std::cerr << std::format(" pc:{:04X}", pc);
	std::cerr << std::format(" a:{:02X}", a);
	std::cerr << std::format(" m:{:02X}", m);
	std::cerr << std::format(" bc:{:04X}", bc);
	std::cerr << std::format(" de:{:04X}", de);
	std::cerr << std::format(" hl:{:04X}", hl);
	std::cerr << std::format(" sp:{:04X}", sp);
	std::cerr << std::format(" *sp:{:04X}", starsp);
	std::cerr << std::format(" {:02X}", starpc1);
	std::cerr << std::format(" {:02X}", starpc2);
	std::cerr << std::format("	{:36s}", disassembly);
	std::cerr << std::format(" FAC: {}", GetFAC());
	std::cerr << std::format("\n");

//	1D25: CD E0 1C  STROUT: CALL STRLIT 35 wide
}

void DumpMemory(uint16_t startDump, uint16_t endDump)
{
	uint8_t *memory = i8080_hal_memory();
	for(uint16_t i = startDump ; i < endDump; i++) {
		std::cerr << std::format("{:04X}: {:02X}\n", i, memory[i]);
	}
}

uint16_t LoadMemory(const std::string &filename, int offset)
{
	uint8_t *memory = i8080_hal_memory();

	memset(memory, 0, 0x10000);
	unsigned int address = 0x0;
	if (filename.ends_with(".bin")) {
		LoadFile(filename, memory, offset);
		address = offset;
	} else if (filename.ends_with(".tap")) {
		address = LoadAltairFile(filename, memory);
	} else {
		std::cerr << std::format("unable to load {}\n", filename);
		exit(1);
	}
	return address;
}

uint16_t ParseExpression(const std::string &expr)
{
	if (expr.size() == 0) return 0;
	if (std::isdigit(expr[0])) {
		return strtoul(expr.c_str(), NULL, 16);
	}
	auto symbol = symbolTable.GetSymbol(expr);
	if (symbol == nullptr) {
		std::cerr << std::format("Can't find symbol {}. Using 0.\n", expr);
		return 0;
	}
	return symbol->value;
}

std::pair<uint16_t, uint16_t> ParseDisassembleArg(const std::string &disassembleArg)
{
	std::stringstream ss(disassembleArg);
	std::string token1;
	std::string token2;
	std::getline(ss, token1, ':');
	std::getline(ss, token2);
	uint16_t low = ParseExpression(token1);
	uint16_t high = ParseExpression(token2);
	return std::make_pair(low, high);
}

struct Trigger {
	std::string token1;
	std::string token2;
	std::string token3;

	bool triggerCheck = false;
	uint16_t triggerAddress;
	uint16_t start;
	uint16_t stop;
	std::vector<uint8_t> bytes;

	// e.g. "RUN:1000:2000"
	Trigger(const std::string &triggerArg) {
		std::stringstream ss(triggerArg);
		std::getline(ss, token1, ':');
		std::getline(ss, token2, ':');
		std::getline(ss, token3);
		triggerAddress = ParseExpression(token1);
		start = ParseExpression(token2);
		stop = ParseExpression(token3);
		std::cerr << std::format("setting up monitor to trigger on PC {:04X} from {:04X} to {:04X}\n", triggerAddress, start, stop);
	}
	uint16_t Check(uint16_t pc) {
		if (!triggerCheck && pc == triggerAddress) {
			triggerCheck = true;
			// hack for lack of expression parser with unary * operator
			start = i8080_hal_memory_read_word(start);
			stop = i8080_hal_memory_read_word(stop);
			for (int addr = start; addr < stop; addr++) {
				bytes.push_back(i8080_hal_memory_read_byte(addr));
			}
			std::cerr << std::format("triggered at PC {:04X}, monitoring memory from {:04X} to {:04X}\n", triggerAddress, start, stop);
		}
		if (triggerCheck) {
			for (int addr = start; addr < stop; addr++) {
				if (bytes[addr - start] != i8080_hal_memory_read_byte(addr)) {
					return addr;
				}
			}
		}
		return 0;
	}
};

void Execute(
	uint16_t address,
	bool dump,
	int instructionCountLimit,
	int sleep,
	int breakAt,
	int breakCount,
	std::vector<Trigger> &triggers,
	const std::vector<int> &watchBytes,
	const std::vector<int> &watchWords,
	uint16_t startDump,
	uint16_t endDump,
	bool setSymbol)
{
	std::cerr << std::format("Starting program execution at {:04X}\n", address);

	uint8_t *memory = i8080_hal_memory();
	i8080_state cpu;
	int instructionCount = 0;

	std::vector<uint8_t> watchByteValues(watchBytes.size());
	std::vector<uint16_t> watchWordValues(watchWords.size());
	for(auto i = 0; i < watchBytes.size(); i++) {
		watchByteValues[i] = i8080_hal_memory_read_byte(watchBytes[i]);
	}

	for(auto i = 0; i < watchWords.size(); i++) {
		watchWordValues[i] = i8080_hal_memory_read_byte(watchWords[i]);
	}

	i8080_init(&cpu);

	i8080_jump(&cpu, 0x0);

	if (address != 0xffff) i8080_jump(&cpu, address);

	while(true) {
		int const pc = i8080_pc(&cpu);

		for(auto i = 0; i < watchBytes.size(); i++) {
			if (watchByteValues[i] != i8080_hal_memory_read_byte(watchBytes[i])) {
				std::cerr << std::format("Byte at address {:04X} changed from {:02X} to {:02X}\n",
						watchBytes[i], watchByteValues[i], i8080_hal_memory_read_byte(watchBytes[i]));
				DumpState(&cpu, memory, instructionCount, setSymbol);
				watchByteValues[i] = i8080_hal_memory_read_byte(watchBytes[i]);
			}
		}

		for(auto i = 0; i < watchWords.size(); i++) {
			if (watchWordValues[i] != i8080_hal_memory_read_byte(watchWords[i])) {
				std::cerr << std::format("Word at address {:04X} changed from {:04X} to {:04X}\n",
						watchWords[i], watchWordValues[i], i8080_hal_memory_read_byte(watchWords[i]));
				DumpState(&cpu, memory, instructionCount, setSymbol);
				watchWordValues[i] = i8080_hal_memory_read_byte(watchWords[i]);
			}
		}

		if (pc == breakAt) {
			std::cerr << std::format("Reached break point at {:04X}\n", pc);
			DumpState(&cpu, memory, instructionCount, setSymbol);
			if (startDump < endDump) {
				DumpMemory(startDump, endDump);
			}
			breakCount--;
			if (breakCount <= 0) {
				exit(0);
			}
		}
		if (memory[pc] == 0x76) {
			std::cerr << std::format("HLT at {:04X}\n", pc);
			exit(0);
		}

		for (auto &trigger : triggers) {
			auto check = trigger.Check(uint16_t(pc));
			if (check > 0) {
				std::cerr << std::format("Detected trigger byte change at {:04X}\n", check);
				DumpState(&cpu, memory, instructionCount, setSymbol);
				exit(1);
			}
		}
		if(dump) DumpState(&cpu, memory, instructionCount, setSymbol);
		i8080_instruction(&cpu);
		instructionCount++;
		if (instructionCountLimit > 0 && instructionCount >= instructionCountLimit) {
			DumpState(&cpu, memory, instructionCount, setSymbol);
			if (startDump < endDump) DumpMemory(startDump, endDump);
			return;
		}
		if (sleep) {
			std::chrono::microseconds microseconds(sleep);
			std::this_thread::sleep_for(microseconds);
		}
	}
}

void Disassemble(const std::pair<uint16_t, uint16_t> &disassembleArg, bool setSymbol)
{
	uint8_t *memory = i8080_hal_memory();
	std::cerr << std::format("Disassembling from {:04X} to {:04X}\n",
		disassembleArg.first,
		disassembleArg.second
	);

	for(uint16_t pc = disassembleArg.first ; pc < disassembleArg.second ; /* */) {
		uint8_t starpc = memory[pc];
		uint8_t starpc1 = memory[pc+1];
		uint8_t starpc2 = memory[pc+2];
		auto disassembly = instructions8085[starpc].AsString(
			pc,
			starpc1,
			starpc2,
			symbolTable,
			setSymbol);
		std::cout << disassembly << std::endl;
		pc += instructions8085[starpc].GetLength(pc, symbolTable);
	}
}

void Usage(int argc, char **argv) {
	std::cerr << std::format("usage {} [options] <file.bin or file.tap>\n", argv[0]);
	std::cerr << "	file.bin -> pure binary file\n";
	std::cerr << "	file.tap -> in Altair papertape/cassette format\n";
	std::cerr << "-b <hex>      -> break when the program counter reaches this address\n";
	std::cerr << "-B <integer>  -> break after <integer> times hitting the breakpoint - default value is 1\n";
	std::cerr << "-d            -> dump registers and disassembly every instruction cycle\n";
	std::cerr << "-D <low:high> -> disassemble file.bin or file.tap in the range low..high (both hex)\n";
	std::cerr << "-i <integer>  -> stop running after <integer> instruction cycles\n";
	std::cerr << "-o <hex>      -> load bin file at offset <hex> (also jump there on simulator start\n";
	std::cerr << "-s <integer>  -> sleep <integer> microseconds between instruction cycles\n";
	std::cerr << "-S <symbols>  -> load symbols (.sym file) from given file\n";
	std::cerr << "-w <hex>      -> watch to see when the byte at address <hex> changes\n";
	std::cerr << "-W <hex>      -> watch to see when the 16 bit word at address <hex> changes\n";
	std::cerr << "-z <low:high> -> dump the binary code in the range low..high (both hex) on every breakpoint\n";
	std::cerr << "-Z <low:high> -> dump the binary code in the range low..high (both hex)\n";
	exit(1);
}

int main(int argc, char **argv)
{
	setbuf(stdout, NULL);
	std::ios_base::sync_with_stdio(true);
	int opt;
#if 0
	Single fp0(/* 0,0,0,0, */ 0, 0, 0x60, 0x80);
	Single fp1(64, 46, 148, 116);	// -.0001413161
	Single fp2(112, 79, 46, 119);	// .001329882

	std::cout << std::format("value: {} Exponent: {} Mantissa: {}", fp0.Value(), fp0.Exponent(), fp0.Mantissa()) << std::endl;
	std::cout << std::format("value: {} Exponent: {} Mantissa: {}", fp1.Value(), fp1.Exponent(), fp1.Mantissa()) << std::endl;
	std::cout << std::format("value: {} Exponent: {} Mantissa: {}", fp2.Value(), fp2.Exponent(), fp2.Mantissa()) << std::endl;
	exit(0);
#endif

	std::string breakAtStr;
	int breakAt = -1;
	int breakCount = 1;
	bool disassembleFlag = false;
	std::string disassembleArgString;
	std::pair<uint16_t, uint16_t> disassembleArg(0,0);
	bool dump = false;
	int instructionCountLimit = -1;
	uint16_t offset = 0;
	int sleep = 0;
	std::string symbolFile;
	std::vector<Trigger> triggers;
	std::vector<std::string> triggerStr;
	std::vector<std::string> watchByteStr;
	std::vector<int> watchBytes;
	std::vector<std::string> watchWordStr;
	std::vector<int> watchWords;
	bool zDumpFlag = false;
	std::string zDumpArgString;
	std::pair<uint16_t, uint16_t> zDumpArg(0,0);
	std::string zDumpArgRunTimeString;
	std::pair<uint16_t, uint16_t> zDumpArgRunTime(0,0);

	while ((opt = getopt(argc, argv, "b:B:dD:i:o:s:S:T:w:W:z:Z:")) != -1) {
		switch(opt) {
			case 'b': breakAtStr = optarg; break;
			case 'B': breakCount = strtoul(optarg, NULL, 10); break;
			case 'd': dump = true; break;
			case 'D': disassembleFlag = true; disassembleArgString = optarg; break;
			case 'i': instructionCountLimit = strtoul(optarg, NULL, 10); break;
			case 'o': offset = strtoul(optarg, NULL, 16); break;
			case 's': sleep = strtoul(optarg, NULL, 10); break;
			case 'S': symbolFile = optarg; break;
			case 'T': triggerStr.push_back(optarg); break;
			case 'w': watchByteStr.push_back(optarg); break;
			case 'W': watchWordStr.push_back(optarg); break;
			case 'z': zDumpArgRunTimeString = optarg; break;
			case 'Z': zDumpFlag = true; zDumpArgString = optarg; break;
			default: Usage(argc, argv);
		}
	}

	std::string filename;
	uint16_t address = 0x0;

	if(optind < argc) {
		filename = argv[optind];
		address = LoadMemory(filename, address);
		std::cerr << std::format("Loaded memory from {} - start address: {:04X}\n", filename, address);
	} else {
		Usage(argc, argv);
	}

	if (symbolFile.size()) {
		symbolTable.Load(symbolFile);
	}

	if (disassembleFlag == true) {
		disassembleArg = ParseDisassembleArg(disassembleArgString);
	}

	if (zDumpFlag == true) {
		zDumpArg = ParseDisassembleArg(zDumpArgString);
	}

	if (breakAtStr.size()) {
		breakAt = ParseExpression(breakAtStr);
	}

	for (auto i = 0; i < watchByteStr.size() ; i++) {
		watchBytes.push_back(ParseExpression(watchByteStr[i]));
	}

	for (auto i = 0; i < triggerStr.size() ; i++) {
		triggers.emplace_back(triggerStr[i]);
	}

	for (auto i = 0; i < watchWordStr.size() ; i++) {
		watchWords.push_back(ParseExpression(watchWordStr[i]));
	}

	zDumpArgRunTime = ParseDisassembleArg(zDumpArgRunTimeString);

	if (zDumpFlag) {
		uint8_t *memory = i8080_hal_memory();
		for(auto i = zDumpArg.first; i < zDumpArg.second; i++) {
			putchar(*(memory + i));
		}
		exit(0);
	}

	if (disassembleFlag) {
		Disassemble(disassembleArg, true || symbolFile.size() == 0);
	} else {
		Execute(
			address,
			dump,
			instructionCountLimit,
			sleep,
			breakAt,
			breakCount,
			triggers,
			watchBytes,
			watchWords,
			zDumpArgRunTime.first,
			zDumpArgRunTime.second,
			symbolFile.size() == 0);
	}
	symbolTable.Save("foo.sym");
	return 0;
}
