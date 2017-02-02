// This file is part of halimede. It is subject to the licence terms in the COPYRIGHT file found in the top-level directory of this distribution and at https://raw.githubusercontent.com/pallene/halimede/master/COPYRIGHT. No part of halimede, including this file, may be copied, modified, propagated, or distributed except according to the terms contained in the COPYRIGHT file.
// Copyright © 2015 The developers of halimede. See the COPYRIGHT file in the top-level directory of this distribution and at https://raw.githubusercontent.com/pallene/halimede/master/COPYRIGHT.


// #include <stdio.h>
// #include "lua.h"
// #include "lauxlib.h"
// #include "lualib.h"
//
//
// int main(const int argc, char **argv)
// {
// 	lua_State *L = luaL_newstate();
// 	luaL_openlibs(L);
//
// 	// Create _G.arg
// 	int argument;
// 	lua_createtable(L, argc, 1);
// 	for (argument = 0; argument < argc; argument++)
// 	{
// 		lua_pushstring(L, argv[argument]);
// 		lua_rawseti(L, -2, argument);
// 	}
// 	lua_setglobal(L, "arg");
// 	luaL_checkstack(L, argc - 1, "Stack can not grow");
//
// 	// Run
// 	lua_getglobal(L, "require");
// 	lua_pushliteral(L, "main");
// 	int status = lua_pcall(L, 1, 0, 0);
// 	if (status)
// 	{
// 		fprintf(stderr, "Error:%s\n", lua_tostring(L, -1));
// 		return 1;
// 	}
// 	return 0;
// }



// gcc -static -static-libgcc -Wall -o main __main.static.c -lbfd -lz -liberty && ./main

#define _GNU_SOURCE

// Alpine Linux: sudo apk add libc-dev
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <stdbool.h>

#include <unistd.h>
#include <sys/mman.h>

// Alpine Linux: sudo apk add binutils-dev
#define PACKAGE 1
#define PACKAGE_VERSION 1
#include <bfd.h>

// Local Source
#include "whereami/src/whereami.h"
#include "whereami/src/whereami.c"


__attribute__ ((noreturn))
void fail(const char* message)
{
	fprintf(stderr, "Error: %s\n", message);
	exit(2);
}

// Caller is responsible for deallocation
char * getExecutablePath()
{
	int length;
	int directoryNameLength;
	length = wai_getExecutablePath(NULL, 0, &directoryNameLength);
	
	if (length == 0)
	{
		fail("Could not obtain executable path");
	}
	
	char * path = malloc(length + 1);
	if (!path)
	{
		fail("Could not obtain memory for executable path");
	}
	
	wai_getExecutablePath(path, length, &directoryNameLength);
	path[length] = '\0';
	
	return path;
}

bfd * initialiseBfd(const char * path)
{
	bfd_init();
	
	bfd * bfdHandle = bfd_openr(path, NULL);
	if (bfdHandle == NULL)
	{
		fail("Could not open self");
	}
	if (bfd_check_format(bfdHandle, bfd_object) == FALSE)
	{
		bfd_close(bfdHandle);
		fail("Not a regular executable");
	}
	return bfdHandle;
}

typedef struct
{
	asymbol * * symbols;
	long numberOfSymbolPointers;
} SymbolTable;

SymbolTable getBfdSymbolTable(bfd * bfdHandle)
{
	long maximumSymbolTableSize = bfd_get_symtab_upper_bound(bfdHandle);
	if (maximumSymbolTableSize == -1)
	{
		fail("Could not get symbol table size");
	}

	asymbol * * symbols;
	long numberOfSymbolPointers;
	if (maximumSymbolTableSize == 0)
	{
		symbols = NULL;
		numberOfSymbolPointers = 0;
	}
	else
	{
		symbols = malloc(maximumSymbolTableSize);
		if (symbols == NULL)
		{
			fail("Could not allocate memory for symbol table");
		}
	
		numberOfSymbolPointers = bfd_canonicalize_symtab(bfdHandle, symbols);
	}
	
	SymbolTable symbolTable =
	{
		symbols: symbols,
		numberOfSymbolPointers: numberOfSymbolPointers,
	};
	return symbolTable;
}

void freeSymbolTable(SymbolTable symbolTable)
{
	if (symbolTable.numberOfSymbolPointers != 0)
	{
		free((void *) symbolTable.symbols);
	}
}

asymbol * findSymbol(SymbolTable symbolTable, const char * symbolName)
{
	long count = symbolTable.numberOfSymbolPointers;
	for (int symbolIndex = 0; symbolIndex < count; symbolIndex++)
	{
		asymbol * symbol = symbolTable.symbols[symbolIndex];
		if (strcmp(symbol->name, symbolName))
		{
			return symbol;
		}
	}
	return NULL;
}

typedef struct
{
	arelent * * relocations;
	long numberOfRelocationPointers;
} RelocationTable;

RelocationTable getBfsRelocationTable(bfd * bfdHandle, SymbolTable symbolTable, asection * section)
{
	long maximumRelocationTableSize = bfd_get_reloc_upper_bound(bfdHandle, section);
	if (maximumRelocationTableSize == -1)
	{
		fail("Could not get a relocation table size");
	}

	arelent * * relocations;
	long numberOfRelocationPointers;
	if (maximumRelocationTableSize == 0)
	{
		relocations = NULL;
		numberOfRelocationPointers = 0;
	}
	else
	{
		relocations = malloc(maximumRelocationTableSize);
		if (relocations == NULL)
		{
			fail("Could not allocate memory for a relocation table");
		}
	
		numberOfRelocationPointers = bfd_canonicalize_reloc(bfdHandle, section, relocations, symbolTable.symbols);
	}
	
	RelocationTable relocationTable =
	{
		relocations: relocations,
		numberOfRelocationPointers: numberOfRelocationPointers,
	};
	return relocationTable;
}

void freeRelocationTable(RelocationTable relocationTable)
{
	if (relocationTable.numberOfRelocationPointers != 0)
	{
		free((void *) relocationTable.relocations);
	}
}

size_t roundUpToMultipleOfSize(size_t desiredSize, size_t pageSize)
{
	if (desiredSize < pageSize)
	{
		return pageSize;
	}
	
	size_t remainder = desiredSize % pageSize;
	if (remainder == 0)
	{
	  return desiredSize;
	}
	return desiredSize - remainder + pageSize;
}

void * allocateZeroedPageAlignedMemoryThatIsReadableWritableAndExecutable(size_t desiredSize)
{
	size_t pageSize = sysconf(_SC_PAGESIZE);
	size_t roundedUpSize = roundUpToMultipleOfSize(desiredSize, pageSize);
	
	void * memory;
	
	if (posix_memalign(&memory, pageSize, roundedUpSize) != 0)
	{
		fail("Posix memalign failed");
	}

	//if (mprotect(memory, roundedUpSize, PROT_READ | PROT_WRITE | PROT_EXEC) != 0)
	if (mprotect(memory, roundedUpSize, PROT_READ | PROT_WRITE) != 0)
	{
		perror("Error: mprotect failed");
		exit(2);
	}
	
	memset(memory, 0, roundedUpSize);
	
	return memory;
}

typedef struct
{
	unsigned char * memory;
	void * dataStartPointer;
	void * textStartPointer;
	asection * data;
	asection * text;
	RelocationTable dataRelocationTable;
	RelocationTable textRelocationTable;
} DataAndTextSections;

// .text => code => functions
// .data => initialized constants
// .bss => unintialized globals
DataAndTextSections initialiseDataAndTextSectionCopies(bfd * bfdHandle, SymbolTable symbolTable)
{
	// Load and copy .data and .text sections
	// Can return NULL
	// Returns only one if multiple sections present
	asection * data = bfd_get_section_by_name(bfdHandle, ".data");
	size_t dataSize = data->size;
	asection * text = bfd_get_section_by_name(bfdHandle, ".text");
	size_t textSize = text->size;
	size_t dataAndTextSectionMemorySize = dataSize + textSize;
	
	unsigned char * memory = (unsigned char *) allocateZeroedPageAlignedMemoryThatIsReadableWritableAndExecutable(dataAndTextSectionMemorySize);
	
	void * dataStartPointer = memory;
	void * textStartPointer = memory + dataSize;
	bfd_get_section_contents(bfdHandle, data, dataStartPointer, 0, dataSize);
	bfd_get_section_contents(bfdHandle, text, textStartPointer, 0, textSize);
	
	// BFD needs to know these values in order to do relocations
	data->output_offset = (long unsigned int) dataStartPointer;
	text->output_offset = (long unsigned int) textStartPointer;

	
	
	// Relocation tables
	RelocationTable dataRelocationTable = getBfsRelocationTable(bfdHandle, symbolTable, data);
	RelocationTable textRelocationTable = getBfsRelocationTable(bfdHandle, symbolTable, text);
	
	DataAndTextSections dataAndTextSections =
	{
		memory: memory,
		dataStartPointer: dataStartPointer,
		textStartPointer: textStartPointer,
		data: data,
		text: text,
		dataRelocationTable: dataRelocationTable,
		textRelocationTable: textRelocationTable,
	};
	return dataAndTextSections;
}

void freeDataAndTextSections(DataAndTextSections dataAndTextSections)
{
	freeRelocationTable(dataAndTextSections.textRelocationTable);
	freeRelocationTable(dataAndTextSections.dataRelocationTable);
	free((void *) dataAndTextSections.memory);
}

void * functionSymbolAddress(DataAndTextSections dataAndTextSections, asymbol * symbol)
{
	return dataAndTextSections.textStartPointer + symbol->value;
}

void * dataSymbolAddress(DataAndTextSections dataAndTextSections, asymbol * symbol)
{
	return dataAndTextSections.dataStartPointer + symbol->value;
}

void relocateFunctionSymbol(bfd * bfdHandle, SymbolTable symbolTable, DataAndTextSections dataAndTextSections, const char * symbolName, void * replacementFunctionPointer)
{	
	RelocationTable relocationTable = dataAndTextSections.textRelocationTable;
	
	long count = relocationTable.numberOfRelocationPointers;
	for (int relocationIndex = 0; relocationIndex < count; relocationIndex++)
	{
		arelent * relocation = relocationTable.relocations[relocationIndex];
		
		// relocate ('patch') this symbol to point to a different function
		if (!strcmp((*relocation->sym_ptr_ptr)->name, symbolName))
		{
			asymbol * symbol = findSymbol(symbolTable, symbolName);
			if (symbol == NULL)
			{
				fail("Symbol exists in relocations but not symbol table");
			}

			// symbol should, ideally, be undefined
			
			symbol->value = (long unsigned int) replacementFunctionPointer;
			
			dataAndTextSections.text->output_section = (*relocation->sym_ptr_ptr)->section;
			char * ignore;
			bfd_perform_relocation(bfdHandle, relocation, dataAndTextSections.textStartPointer, dataAndTextSections.text, NULL, &ignore);
		}
	}
}

const char * DATA = "HELLO WORLD\n";

int main(const int argc, const char **argv)
{
	const char * executablePath = getExecutablePath();
	bfd * bfdHandle = initialiseBfd(executablePath);
	free((void *) executablePath);
	
	SymbolTable symbolTable = getBfdSymbolTable(bfdHandle);
	DataAndTextSections dataAndTextSections = initialiseDataAndTextSectionCopies(bfdHandle, symbolTable);
	const char * dataSymbolToFind = "DATA";
	asymbol * symbol = findSymbol(symbolTable, dataSymbolToFind);
	
	if (symbol == NULL)
	{
		// could not find it
		printf("FAILED");
		return 1;
	}
	
	// https://sourceware.org/binutils/docs/bfd/typedef-asymbol.html#typedef-asymbol
	bool isLocal = (symbol->flags & BSF_LOCAL) == BSF_LOCAL;
	bool isGlobal = (symbol->flags & BSF_GLOBAL) == BSF_GLOBAL;
	if (isLocal || isGlobal)
	{
		printf("Local or Global\n");
	}

	// void * dataSymbolPointer = dataSymbolAddress(dataAndTextSections, symbol);
	// printf("%lu", (unsigned long) dataSymbolPointer);
	//
	// //char * x = (char *) dataSymbolPointer;
	// //printf(x);
	
	
	freeDataAndTextSections(dataAndTextSections);
	freeSymbolTable(symbolTable);
	bfd_close_all_done(bfdHandle);
	
	return EXIT_SUCCESS;
}





// #include <stdlib.h>
// #include <string.h>
//
// void load_module(lua_State * L, const char * moduleName)
// {
// 	// Need to put a function(modname) into package.preloaded[modname]
// 	// This function(modname) is a 'loader' function
// 	// It returns a module object
// 	// In our case, function(modname) needs
//
// 	const char * byteCode = byteCodeFromModuleName(moduleName)
// 	if (byteCode == NULL)
// 	{
// 		return
// 	}
// 	luaL_loadstring(L, byteCode)
// }
//
// #define ModuleNameToByteCodeTableNumberOfPairs (sizeof(ModuleNameToByteCodeTable) / sizeof(ModuleNameToByteCodePair))
//
// typedef struct
// {
// 	const char * moduleName;
// 	const char * byteCode;
// } ModuleNameToByteCodePair;
//
// const char luaJIT_BC_halimede_init[] = {0,2,3};
// const char luaJIT_BC_halimede_enumeration[] = {9,8,7,6};
//
// static ModuleNameToByteCodePair ModuleNameToByteCodeTable[] =
// {
//     { "halimede.init", luaJIT_BC_halimede_init },
// 	{ "halimede.enumeration", luaJIT_BC_halimede_enumeration },
// };
//
// const char * byteCodeFromModuleName(const char * moduleName)
// {
//     int index;
//     for (index = 0; index < ModuleNameToByteCodeTableNumberOfPairs; index++)
// 	{
//         ModuleNameToByteCodePair *pair = ModuleNameToByteCodeTable + (index * sizeof(ModuleNameToByteCodePair));
//         if (strcmp(pair->moduleName, moduleName) == 0)
// 		{
//             return pair->byteCode;
// 		}
//     }
//     return NULL;
// }