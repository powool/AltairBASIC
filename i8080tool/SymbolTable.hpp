#pragma once
#include <cstdint>
#include <fstream>
#include <iostream>
#include <map>
#include <memory>
#include <string>

struct Symbol {
	Symbol(const std::string &label, uint16_t value, char type, int length) :
		label(label), value(value), type(type), length(length) {;}
	std::string label;
	uint16_t	value;
	char type;	// 'C' -> code, 'D' -> data, 'W' -> word data (uint16_t), 'U' uninitialized data
	int length;
};
using SymbolPtr = std::shared_ptr<Symbol>;

struct SymbolTable {
	std::map<uint16_t, SymbolPtr> symbols;
	public:

	void Load(std::string symbolFile) {
		std::ifstream in(symbolFile);
		if (!in.good()) {
			std::cerr << std::format("Failed to open file {}\n", symbolFile);
			exit(1);
		}

		while(!in.eof()) {
			std::string label;
			std::string value;
			std::string type;
			in >> label;
			in >> value;
			in >> type;
			if (!in.good()) {
				break;
			}
			uint16_t intValue = strtoul(value.c_str(), NULL, 16);
			if (symbols.find(intValue) != symbols.end()) {
//				std::cerr << std::format("Symbol {} with value {} is overwriting previous value with name {}\n", label, value, symbols[intValue]->label);
			}
			char t = '-';
			if(type.size()) t = type[0];
			symbols[intValue] = std::make_shared<Symbol>(label, intValue, t, 0);
		}
		std::cerr << std::format("Loaded {} symbols from {}\n", symbols.size(), symbolFile);
	}
	void Save(std::string symbolFile) {
		std::ofstream out(symbolFile);
		if (!out.good()) {
			std::cerr << std::format("Failed to open file {}\n", symbolFile);
			exit(1);
		}
		for(auto it : symbols) {
			out << std::format("{}\t{:04x}\t{}\n", it.second->label, it.first, it.second->type);
		}
	}
	SymbolPtr GetSymbol(const std::string &name) {
		// slow, but I don't care
		for(auto it : symbols) {
			if (it.second->label == name) {
				std::cerr << std::format("Found value {:04x} for symbol {}\n", it.second->value, it.second->label);
				return it.second;
			}
		}
		return nullptr;
	}
	SymbolPtr GetSymbol(uint16_t address) {
		auto it = symbols.find(address);
		if (it != symbols.end()) {
			return it->second;
		}
		return nullptr;
	}
	void SetSymbol(const std::string label, uint16_t address, char type, int length) {
		auto newSymbol = std::make_shared<Symbol>(label, address, 'C', 0);
		symbols[address] = newSymbol;
	}

};

