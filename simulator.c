#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// Machine Definitions
#define NUMMEMORY 65536 // maximum number of data words in memory
#define NUMREGS 8 // number of machine registers

#define ADD 0
#define NOR 1
#define LW 2
#define SW 3
#define BEQ 4
#define JALR 5 // will not implemented for Project 3
#define HALT 6
#define NOOP 7

const char* opcode_to_str_map[] = {
    "add",
    "nor",
    "lw",
    "sw",
    "beq",
    "jalr",
    "halt",
    "noop"
};

#define NOOPINSTR (NOOP << 22)

typedef struct IFIDStruct {
    int instr;
	int pcPlus1;
} IFIDType;

typedef struct IDEXStruct {
    int instr;
	int pcPlus1;
	int valA;
	int valB;
	int offset;
} IDEXType;

typedef struct EXMEMStruct {
    int instr;
	int branchTarget;
    int eq;
	int aluResult;
	int valB;
} EXMEMType;

typedef struct MEMWBStruct {
    int instr;
	int writeData;
} MEMWBType;

typedef struct WBENDStruct {
    int instr;
	int writeData;
} WBENDType;

typedef struct stateStruct {
    unsigned int numMemory;
    unsigned int cycles; // number of cycles run so far
	int pc;
	int instrMem[NUMMEMORY];
	int dataMem[NUMMEMORY];
	int reg[NUMREGS];
	IFIDType IFID;
	IDEXType IDEX;
	EXMEMType EXMEM;
	MEMWBType MEMWB;
	WBENDType WBEND;
} stateType;

static inline int opcode(int instruction) {
    return instruction>>22;
}

static inline int field0(int instruction) {
    return (instruction>>19) & 0x7;
}

static inline int field1(int instruction) {
    return (instruction>>16) & 0x7;
}

static inline int field2(int instruction) {
    return instruction & 0xFFFF;
}

// convert a 16-bit number into a 32-bit Linux integer
static inline int convertNum(int num) {
    return num - ( (num & (1<<15)) ? 1<<16 : 0 );
}

void printState(stateType*);
void printInstruction(int);
void readMachineCode(stateType*, char*);

// HELPER FUNCTIONS
int isReadInstr(int instr);
int isWritingOpcode(int instr);
void processField(int instr, int* valA, int* valB, int targetInstr, int writeData);
void dataHazard(int* valA, int* valB, stateType* state);


int main(int argc, char *argv[]) {

    /* Declare state and newState.
       Note these have static lifetime so that instrMem and
       dataMem are not allocated on the stack. */

    static stateType state, newState;

    if (argc != 2) {
        printf("error: usage: %s <machine-code file>\n", argv[0]);
        exit(1);
    }

    readMachineCode(&state, argv[1]);

    /* ------------ Initialize State ------------ */

    // INITIALIZE INSTRUCTIONS TO NOOP

    state.IFID.instr = NOOPINSTR;
    state.IDEX.instr = NOOPINSTR;
    state.EXMEM.instr = NOOPINSTR;
    state.MEMWB.instr = NOOPINSTR;
    state.WBEND.instr = NOOPINSTR;

    /* ------------------- END ------------------ */

    newState = state;

    while (opcode(state.MEMWB.instr) != HALT) {
        printState(&state);

        newState.cycles += 1;

        /* ---------------------- IF stage --------------------- */

        newState.IFID.instr = state.instrMem[state.pc];
        newState.IFID.pcPlus1 = state.pc + 1;
        newState.pc = state.pc + 1;

        /* ---------------------- ID stage --------------------- */

        int stalling = 0;

        newState.IDEX.instr = state.IFID.instr;
        newState.IDEX.valA = state.reg[field0(state.IFID.instr)];
        newState.IDEX.valB = state.reg[field1(state.IFID.instr)];
        newState.IDEX.offset = convertNum(field2(state.IFID.instr));
        newState.IDEX.pcPlus1 = state.IFID.pcPlus1;

        switch(opcode(state.IDEX.instr)){
            case LW:
            {
                // Check if our current instruction relies on something loaded, and stall if it does
                // We only care about field 0 if it's a load word, but all other fields if it isn't
                stalling = opcode(state.IFID.instr) == LW ? field0(state.IFID.instr) == field1(state.IDEX.instr):((field0(state.IFID.instr) == field1(state.IDEX.instr)) || (field1(state.IFID.instr) == field1(state.IDEX.instr)));
                if(stalling){
                    // We need to stall
                    newState.IDEX.instr = NOOPINSTR;
                    newState.pc = state.pc;
                    newState.IFID = state.IFID;
                }
                break;
            }

        }
        
        /* ---------------------- EX stage --------------------- */

        int valA = state.IDEX.valA, valB = state.IDEX.valB; // Use variables in case of forwarding so as to not write to state

        // Check for data hazards

        dataHazard(&valA, &valB, &state);

        
        newState.EXMEM.valB = valB;
        newState.EXMEM.branchTarget = state.IDEX.offset + state.IDEX.pcPlus1;

        
        switch(opcode(state.IDEX.instr)){
            case LW:
            case SW:
            newState.EXMEM.aluResult = valA + state.IDEX.offset;
            break;
            case NOR:
            newState.EXMEM.aluResult = ~(valA | valB);
            break;
            case ADD:
            newState.EXMEM.aluResult = valA + valB;
            break;
        }

        newState.EXMEM.eq = (valA == valB);

        newState.EXMEM.instr = state.IDEX.instr;

        /* --------------------- MEM stage --------------------- */

        newState.MEMWB.instr = state.EXMEM.instr;

        if(state.EXMEM.eq && (opcode(state.EXMEM.instr) == BEQ)){
            // Set PC to branch target
            newState.pc = state.EXMEM.branchTarget;
            
            // Squash
            newState.IFID.instr = NOOPINSTR;
            newState.IDEX.instr = NOOPINSTR;
            newState.EXMEM.instr = NOOPINSTR;

            newState.MEMWB.writeData = state.EXMEM.aluResult;
        } else{
            // Continue as normal

            switch(opcode(state.EXMEM.instr)){
                case LW:
                newState.MEMWB.writeData = state.dataMem[state.EXMEM.aluResult];
                break;
                case SW:
                newState.dataMem[state.EXMEM.aluResult] = state.EXMEM.valB;
                newState.MEMWB.writeData = state.dataMem[state.EXMEM.aluResult];
                break;
                default:
                newState.MEMWB.writeData = state.EXMEM.aluResult;
                break;
            }
        }

        /* ---------------------- WB stage --------------------- */

        newState.WBEND.writeData = state.MEMWB.writeData;
        newState.WBEND.instr = state.MEMWB.instr;

        switch(opcode(state.MEMWB.instr)){
            case NOR:
            case ADD:
            newState.reg[field2(state.MEMWB.instr)] = state.MEMWB.writeData;
            break;
            case LW:
            newState.reg[field1(state.MEMWB.instr)] = state.MEMWB.writeData;
            break;
        }

        /* ------------------------ END ------------------------ */
        state = newState; /* this is the last statement before end of the loop. It marks the end
        of the cycle and updates the current state with the values calculated in this cycle */
    }
    printf("Machine halted\n");
    printf("Total of %d cycles executed\n", state.cycles);
    printf("Final state of machine:\n");
    printState(&state);
}

int isReadInstr(int instr){
    int opc = opcode(instr);
    return (opc != JALR && opc != HALT && opc != NOOP);
}

int isWritingOpcode(int instr){ // Return the field we want (1 = field1, 2 = field2)
    int opc = opcode(instr);
    return opc == LW ? 1 : ((opc == ADD || opc == NOR) ? 2:0);
}

void processField(int instr, int* valA, int* valB, int targetInstr, int writeData){
    int field = isWritingOpcode(targetInstr);

    switch(field){
        case 1:
        {
            // Field 1, check if current instruction field0 or field1 rely on it
            if(field0(instr) == field1(targetInstr)){
                *valA = writeData;
            }
            if(field1(instr) == field1(targetInstr)){
                *valB = writeData;
            }
            break;
        }

        case 2:
        {
            // Field 2, check if current instruction field0 or field1 rely on it
            if(field0(instr) == field2(targetInstr)){
                *valA = writeData;
            }
            if(field1(instr) == field2(targetInstr)){
                *valB = writeData;
            }
            break;
        }
    }
}

void dataHazard(int* valA, int* valB, stateType* state){
    // Will process data hazard and forwarding (checks WBEND, MEMWB, then EXMEM writeDatas and formatting properly based on the current instruction)

    int instr = state->IDEX.instr; // Since we will be checking in the IDEX stage

    if(!isReadInstr(instr)) return; // If we don't care about data forwarding

    processField(instr, valA, valB, state->WBEND.instr, state->WBEND.writeData);
    processField(instr, valA, valB, state->MEMWB.instr, state->MEMWB.writeData);
    processField(instr, valA, valB, state->EXMEM.instr, state->EXMEM.aluResult);
}

/*
* DO NOT MODIFY ANY OF THE CODE BELOW.
*/

void printInstruction(int instr) {
    const char* instr_opcode_str;
    int instr_opcode = opcode(instr);
    if(ADD <= instr_opcode && instr_opcode <= NOOP) {
        instr_opcode_str = opcode_to_str_map[instr_opcode];
    }

    switch (instr_opcode) {
        case ADD:
        case NOR:
        case LW:
        case SW:
        case BEQ:
            printf("%s %d %d %d", instr_opcode_str, field0(instr), field1(instr), convertNum(field2(instr)));
            break;
        case JALR:
            printf("%s %d %d", instr_opcode_str, field0(instr), field1(instr));
            break;
        case HALT:
        case NOOP:
            printf("%s", instr_opcode_str);
            break;
        default:
            printf(".fill %d", instr);
            return;
    }
}

void printState(stateType *statePtr) {
    printf("\n@@@\n");
    printf("state before cycle %d starts:\n", statePtr->cycles);
    printf("\tpc = %d\n", statePtr->pc);

    printf("\tdata memory:\n");
    for (int i=0; i<statePtr->numMemory; ++i) {
        printf("\t\tdataMem[ %d ] = 0x%08X\n", i, statePtr->dataMem[i]);
    }
    printf("\tregisters:\n");
    for (int i=0; i<NUMREGS; ++i) {
        printf("\t\treg[ %d ] = %d\n", i, statePtr->reg[i]);
    }

    // IF/ID
    printf("\tIF/ID pipeline register:\n");
    printf("\t\tinstruction = 0x%08X ( ", statePtr->IFID.instr);
    printInstruction(statePtr->IFID.instr);
    printf(" )\n");
    printf("\t\tpcPlus1 = %d", statePtr->IFID.pcPlus1);
    if(opcode(statePtr->IFID.instr) == NOOP){
        printf(" (Don't Care)");
    }
    printf("\n");
    
    // ID/EX
    int idexOp = opcode(statePtr->IDEX.instr);
    printf("\tID/EX pipeline register:\n");
    printf("\t\tinstruction = 0x%08X ( ", statePtr->IDEX.instr);
    printInstruction(statePtr->IDEX.instr);
    printf(" )\n");
    printf("\t\tpcPlus1 = %d", statePtr->IDEX.pcPlus1);
    if(idexOp == NOOP){
        printf(" (Don't Care)");
    }
    printf("\n");
    printf("\t\treadRegA = %d", statePtr->IDEX.valA);
    if (idexOp >= HALT || idexOp < 0) {
        printf(" (Don't Care)");
    }
    printf("\n");
    printf("\t\treadRegB = %d", statePtr->IDEX.valB);
    if(idexOp == LW || idexOp > BEQ || idexOp < 0) {
        printf(" (Don't Care)");
    }
    printf("\n");
    printf("\t\toffset = %d", statePtr->IDEX.offset);
    if (idexOp != LW && idexOp != SW && idexOp != BEQ) {
        printf(" (Don't Care)");
    }
    printf("\n");

    // EX/MEM
    int exmemOp = opcode(statePtr->EXMEM.instr);
    printf("\tEX/MEM pipeline register:\n");
    printf("\t\tinstruction = 0x%08X ( ", statePtr->EXMEM.instr);
    printInstruction(statePtr->EXMEM.instr);
    printf(" )\n");
    printf("\t\tbranchTarget %d", statePtr->EXMEM.branchTarget);
    if (exmemOp != BEQ) {
        printf(" (Don't Care)");
    }
    printf("\n");
    printf("\t\teq ? %s", (statePtr->EXMEM.eq ? "True" : "False"));
    if (exmemOp != BEQ) {
        printf(" (Don't Care)");
    }
    printf("\n");
    printf("\t\taluResult = %d", statePtr->EXMEM.aluResult);
    if (exmemOp > SW || exmemOp < 0) {
        printf(" (Don't Care)");
    }
    printf("\n");
    printf("\t\treadRegB = %d", statePtr->EXMEM.valB);
    if (exmemOp != SW) {
        printf(" (Don't Care)");
    }
    printf("\n");

    // MEM/WB
	int memwbOp = opcode(statePtr->MEMWB.instr);
    printf("\tMEM/WB pipeline register:\n");
    printf("\t\tinstruction = 0x%08X ( ", statePtr->MEMWB.instr);
    printInstruction(statePtr->MEMWB.instr);
    printf(" )\n");
    printf("\t\twriteData = %d", statePtr->MEMWB.writeData);
    if (memwbOp >= SW || memwbOp < 0) {
        printf(" (Don't Care)");
    }
    printf("\n");     

    // WB/END
	int wbendOp = opcode(statePtr->WBEND.instr);
    printf("\tWB/END pipeline register:\n");
    printf("\t\tinstruction = 0x%08X ( ", statePtr->WBEND.instr);
    printInstruction(statePtr->WBEND.instr);
    printf(" )\n");
    printf("\t\twriteData = %d", statePtr->WBEND.writeData);
    if (wbendOp >= SW || wbendOp < 0) {
        printf(" (Don't Care)");
    }
    printf("\n");

    printf("end state\n");
    fflush(stdout);
}

// File
#define MAXLINELENGTH 1000 // MAXLINELENGTH is the max number of characters we read

void readMachineCode(stateType *state, char* filename) {
    char line[MAXLINELENGTH];
    FILE *filePtr = fopen(filename, "r");
    if (filePtr == NULL) {
        printf("error: can't open file %s", filename);
        exit(1);
    }

    printf("instruction memory:\n");
    for (state->numMemory = 0; fgets(line, MAXLINELENGTH, filePtr) != NULL; ++state->numMemory) {
        if (sscanf(line, "%x", state->instrMem+state->numMemory) != 1) {
            printf("error in reading address %d\n", state->numMemory);
            exit(1);
        }
        printf("\tinstrMem[ %d ] = 0x%08X ( ", state->numMemory, 
            state->instrMem[state->numMemory]);
        printInstruction(state->dataMem[state->numMemory] = state->instrMem[state->numMemory]);
        printf(" )\n");
    }
}
