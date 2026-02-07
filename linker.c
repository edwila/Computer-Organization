#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <ctype.h>

#define MAXSIZE 500
#define MAXLINELENGTH 1000
#define MAXFILES 6

typedef struct FileData FileData;
typedef struct SymbolTableEntry SymbolTableEntry;
typedef struct RelocationTableEntry RelocationTableEntry;
typedef struct CombinedFiles CombinedFiles;

static inline void printHexToFile(FILE *, int);
static inline void throwError(char*);
static inline unsigned int calculateOffset(const SymbolTableEntry*, int, int, const CombinedFiles*);
static inline RelocationTableEntry* getReloc(FileData* files, int numFiles, const char* label);
static inline SymbolTableEntry* getSymbol(FileData* files, int, const char* label);
static inline SymbolTableEntry* getSymbolCombined(CombinedFiles* file, const char* label);

struct SymbolTableEntry {
	char label[7];
	char location;
	unsigned int offset;
};

struct RelocationTableEntry {
    unsigned int file;
	unsigned int offset;
	char inst[6];
	char label[7];
};

struct FileData {
	unsigned int textSize;
	unsigned int dataSize;
	unsigned int symbolTableSize;
	unsigned int relocationTableSize;
	unsigned int textStartingLine; // in final executable
	unsigned int dataStartingLine; // in final executable
	int text[MAXSIZE];
	int data[MAXSIZE];
	SymbolTableEntry symbolTable[MAXSIZE];
	RelocationTableEntry relocTable[MAXSIZE];
};

struct CombinedFiles {
	unsigned int textSize;
	unsigned int dataSize;
	unsigned int symbolTableSize;
	unsigned int relocationTableSize;
	int text[MAXSIZE * MAXFILES];
	int data[MAXSIZE * MAXFILES];
	SymbolTableEntry symbolTable[MAXSIZE * MAXFILES];
	RelocationTableEntry relocTable[MAXSIZE * MAXFILES];
	unsigned int expectedTextSize;
	unsigned int expectedDataSize;
};

int main(int argc, char *argv[]) {
	char *inFileStr, *outFileStr;
	FILE *inFilePtr, *outFilePtr; 
	unsigned int i, j;

    if (argc <= 2 || argc > 8 ) {
        printf("error: usage: %s <MAIN-object-file> ... <object-file> ... <output-exe-file>, with at most 5 object files\n",
				argv[0]);
		exit(1);
	}

	outFileStr = argv[argc - 1];

	outFilePtr = fopen(outFileStr, "w");
	if (outFilePtr == NULL) {
		printf("error in opening %s\n", outFileStr);
		exit(1);
	}

	FileData files[MAXFILES];

	CombinedFiles combined;

	combined.expectedDataSize = 0;
	combined.expectedTextSize = 0;

  // read in all files and combine into a "master" file
	for (i = 0; i < argc - 2; ++i) {
		inFileStr = argv[i+1];

		inFilePtr = fopen(inFileStr, "r");
		printf("opening %s\n", inFileStr);

		if (inFilePtr == NULL) {
			printf("error in opening %s\n", inFileStr);
			exit(1);
		}

		char line[MAXLINELENGTH];
		unsigned int textSize, dataSize, symbolTableSize, relocationTableSize;

		// parse first line of file
		fgets(line, MAXSIZE, inFilePtr);
		sscanf(line, "%d %d %d %d",
				&textSize, &dataSize, &symbolTableSize, &relocationTableSize);

		files[i].textSize = textSize;
		files[i].dataSize = dataSize;
		files[i].symbolTableSize = symbolTableSize;
		files[i].relocationTableSize = relocationTableSize;

		combined.expectedDataSize += dataSize;
		combined.expectedTextSize += textSize;

		// read in text section
		int instr;
		for (j = 0; j < textSize; ++j) {
			fgets(line, MAXLINELENGTH, inFilePtr);
			instr = strtol(line, NULL, 0);
			files[i].text[j] = instr;
		}

		// read in data section
		int data;
		for (j = 0; j < dataSize; ++j) {
			fgets(line, MAXLINELENGTH, inFilePtr);
			data = strtol(line, NULL, 0);
			files[i].data[j] = data;
		}

		// read in the symbol table
		char label[7];
		char type;
		unsigned int addr;
		for (j = 0; j < symbolTableSize; ++j) {
			fgets(line, MAXLINELENGTH, inFilePtr);
			sscanf(line, "%s %c %d",
					label, &type, &addr);
			files[i].symbolTable[j].offset = addr;
			strcpy(files[i].symbolTable[j].label, label);
			files[i].symbolTable[j].location = type;
		}

		// read in relocation table
		char opcode[7];
		for (j = 0; j < relocationTableSize; ++j) {
			fgets(line, MAXLINELENGTH, inFilePtr);
			sscanf(line, "%d %s %s",
					&addr, opcode, label);
			files[i].relocTable[j].offset = addr;
			strcpy(files[i].relocTable[j].inst, opcode);
			strcpy(files[i].relocTable[j].label, label);
			files[i].relocTable[j].file	= i;
		}
		fclose(inFilePtr);
	} // end reading files

	// *** INSERT YOUR CODE BELOW ***
	//    Begin the linking process
	//    Happy coding!!!

	// Initialize combined files
	combined.textSize = 0;
	combined.dataSize = 0;
	combined.symbolTableSize = 0;
	combined.relocationTableSize = 0;

	for(int i = 0; i < argc - 2; i++){
		// Loop through all the files

		// Set the starting lines
		files[i].textStartingLine = combined.textSize;
		files[i].dataStartingLine = combined.dataSize;

		int textPreWrite = combined.textSize;
		int dataPreWrite = combined.dataSize;


		combined.textSize += files[i].textSize; // Increment textSize
		combined.dataSize += files[i].dataSize; // Increment dataSize

		// Now we can copy the text over
		for(int k = textPreWrite; k < combined.textSize; k++){
			// Write the text from the individual file to the combined file
			combined.text[k] = files[i].text[k - textPreWrite]; // k will be out of bounds, so we need to do k-(combinedTextSize - files[i].textSize)
		} // Finished copying the text over, now we'll do the same with data

		for(int k = dataPreWrite; k < combined.dataSize; k++){
			// Write the data from the individual file to the combined file
			combined.data[k] = files[i].data[k - dataPreWrite]; // k will be out of bounds, so we need to do k-(combinedTextSize - files[i].textSize)
		} // Finished copying the data over

		// Now we need to do the cringe table stuff

		// We shall start with the symbol table
		for(int k = 0; k < files[i].symbolTableSize; k++){
			// For each entry in the symbol table, see if it exists in the combined symbol table

			char fileLoc = files[i].symbolTable[k].location;

			if(!strcmp(files[i].symbolTable[k].label, "Stack") && fileLoc != 'U'){
				throwError("Error: Defining Stack label.\n");
			}

			int creationFlag = 0;

			for(int existing = 0; existing < combined.symbolTableSize; existing++){
				if(!strcmp(files[i].symbolTable[k].label, combined.symbolTable[existing].label)){
					// Found the label in both the symbol table of the file AND the combined. One should be
					// undefined. If both undefined, then exit(1) for duplicate label.
					char combinedLoc = combined.symbolTable[existing].location;
					
					if((fileLoc != 'U') && (combinedLoc != 'U')){
						throwError("Error: Duplicate label.\n");
					} else if((combinedLoc == 'U' && fileLoc != 'U')){
						// Exists in one of them
						combined.symbolTable[existing].location = fileLoc;
						combined.symbolTable[existing].offset = calculateOffset(files[i].symbolTable+k, textPreWrite, dataPreWrite, &combined);
					}
					creationFlag = 1;
				}
			}

			if(!creationFlag){
				// Create the new entry into the symbol table
				//appendSymbol(combined, files[i].symbolTable[k].label, fileLoc);
				SymbolTableEntry* entry = combined.symbolTable+combined.symbolTableSize;
					
				strcpy(entry->label, files[i].symbolTable[k].label);
				entry->offset = ((fileLoc == 'T' || fileLoc == 'D') ? calculateOffset(files[i].symbolTable+k, textPreWrite, dataPreWrite, &combined) : files[i].symbolTable[k].offset);
				entry->location = fileLoc;
				combined.symbolTableSize++;
			}
		}

	}

	for(int syEntry = 0; syEntry < combined.symbolTableSize; syEntry++){
		SymbolTableEntry* entry = combined.symbolTable+syEntry;
		
		if(entry->location == 'U' && strcmp(entry->label, "Stack")){
			// If it's undefined and isn't the Stack
			throwError("Error: Undefined label (that isn't Stack)!\n");
		} else if(!strcmp(entry->label, "Stack")){
			entry->offset = combined.expectedTextSize+combined.expectedDataSize;
		}
	}

	for(int i = 0; i < argc - 2; i++){
		// And LAST BUT NOT LEAST, the relocation table :cry: (has to be its own loop because we wanna make sure the symbol table is completed)
		for(int k = 0; k < files[i].relocationTableSize; k++){
			SymbolTableEntry* entry = getSymbol(files+i, argc-2, files[i].relocTable[k].label);
			RelocationTableEntry* relEntry = files[i].relocTable+k;

			char symbolLoc = entry != NULL ? entry->location : 'U';

			// 2 cases: .fill or regular instruction
			if(isupper(relEntry->label[0]) && symbolLoc == 'U'){
				// Global label that exists in our symbol table, we can just grab the offset
				// 2 cases: text or data. It's only data if it's a .fill instruction
				SymbolTableEntry* two = getSymbolCombined(&combined, relEntry->label);
				
				if(strcmp(relEntry->inst, ".fill")){
					// Case 1: text
					combined.text[files[i].textStartingLine + relEntry->offset] += two->offset;
				} else{
					// Case 2: data
					combined.data[files[i].dataStartingLine + relEntry->offset] += two->offset;
				}
			} 
			else if(!strcmp(relEntry->inst, ".fill")){
				// Case 1
				// Update combined's data section with the new offset
				// If the offset is greater than or equal to the file's text size, 
				// then the label exists in the data section (calculated as textSize + skipping the other data sections - the text size of that file)
				// otherwise, it's just the text starting line
				combined.data[files[i].dataStartingLine + relEntry->offset] += ((files[i].data[relEntry->offset] >= files[i].textSize) ? (combined.textSize + files[i].dataStartingLine - files[i].textSize):(files[i].textStartingLine));
			} else {
				int maskedOffset = 0xFFFF & combined.text[files[i].textStartingLine + relEntry->offset];
				combined.text[files[i].textStartingLine + relEntry->offset] += (maskedOffset >= files[i].textSize) ? (combined.textSize + files[i].dataStartingLine - files[i].textSize):(files[i].textStartingLine);
			}
		}
	}

	// Print the text section
    for(int i = 0; i < combined.textSize; i++){
		printf("0x%08X\n", combined.text[i]);
		printHexToFile(outFilePtr, combined.text[i]);
	}


	// Print the data section
	for(int i = 0; i < combined.dataSize; i++){
		printf("0x%08X\n", combined.data[i]);
		printHexToFile(outFilePtr, combined.data[i]);
	}

} // main

// Prints a machine code word in the proper hex format to the file
static inline void 
printHexToFile(FILE *outFilePtr, int word) {
    fprintf(outFilePtr, "0x%08X\n", word);
}

static inline unsigned int 
calculateOffset(const SymbolTableEntry* entry, int preText, int preData, const CombinedFiles* addition){
	if(!strcmp(entry->label, "Stack")){
		return 0; // Will be updated by the code to be the end
	}
	return (entry->location == 'T' ? (entry->offset+preText):(entry->offset+preData+addition->expectedTextSize)); // If it's a data, we wanna do current offset + all of the text + the data before we write the new data
}


static inline void throwError(char* msg) {
    char* defMsg = "Generic error message.\n";
    if (msg != NULL) {
        defMsg = msg;
    }
    printf("%s", defMsg);
    exit(1);
}

static inline SymbolTableEntry* getSymbolCombined(CombinedFiles* file, const char* label){
	for(int i = 0; i < file->symbolTableSize; i++){
		if(!strcmp(file->symbolTable[i].label, label)){
			return file->symbolTable+i;
		}
	}
	return NULL;
}

static inline SymbolTableEntry* getSymbol(FileData* file, int numFiles, const char* label){
	for(int i = 0; i < file->symbolTableSize; i++){
		if(!strcmp(file->symbolTable[i].label, label)){
			return file->symbolTable+i;
		}
	}
	return NULL;
}

static inline RelocationTableEntry* getReloc(FileData* files, int numFiles, const char* label){
	for(int i = 0; i < numFiles; i++){
		int upperRange = files[i].relocationTableSize;
		for(int k = 0; k < upperRange; k++){
			if(!strcmp(files[i].relocTable[k].label, label)){
				return files[i].relocTable+k;
			}
		}
	}

	return NULL;
}
