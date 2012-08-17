#define DEBUG_TYPE "TraceMem"
#include "llvm/Pass.h"
#include "llvm/Module.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/ADT/Statistic.h"
#include "llvm/Instructions.h"
#include "llvm/Constants.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/InstIterator.h"
#include "llvm/LLVMContext.h"
#include "llvm/Transforms/Utils/BasicBlockUtils.h"
#include "llvm/ValueSymbolTable.h"
#include "llvm/Support/Casting.h"
#include <map>
#include <queue>
#include <vector>
#include <set>
#include <algorithm>
#include <stack>

using namespace llvm;

#define DEBUG_INFO 1

STATISTIC(num_instructions_analyzed,    "Instructions analyzed");
STATISTIC(num_loads,                    "Loads");
STATISTIC(num_loads_instrumented,       "Loads Instrumented");
STATISTIC(num_stores,                   "Stores");
STATISTIC(num_stores_instrumented,      "Stores Instrumented");
STATISTIC(num_functions_analyzed,       "Functions analyzed"); 
STATISTIC(num_functions_skipped,        "Functions skipped"); 

namespace {
    typedef std::set<Function *> FunctionSet;
    typedef std::set<llvm::Instruction *> InstructionSet;
}

namespace TraceMem {
    // TraceMem::TraceMemPass - The first implementation, without getAnalysisUsage.
    struct TraceMemPass : public ModulePass {
        static char ID; // Pass identification, replacement for typeid
        TraceMemPass();
        ~TraceMemPass();

        bool analyzeFunction(Function * const function);
        InstructionSet fAnalyzedInstructions;

        std::multimap<Function *, CallInst *> fFunctionCallSites;
        std::set<CallInst*> fFunctionCalls;

        bool isFromAlloc(Value *v) {
            if (Instruction *I = dyn_cast<Instruction>(v)) {
                if (GetElementPtrInst *gep = dyn_cast<GetElementPtrInst>(I)) {
                    return isFromAlloc(gep->getPointerOperand());
                } else if (BitCastInst *bc = dyn_cast<BitCastInst>(I)) {
                    //TODO: be more rigorous
                    return isFromAlloc(bc->getOperand(0));
                } else if (isa<AllocaInst>(I)) {
                    return true;
                }
                return false;
            } else if (Argument *arg = dyn_cast<Argument>(v)) {
                Function *func = arg->getParent();
                auto ret = fFunctionCallSites.equal_range(func);
                for (auto callSiteIt=ret.first; callSiteIt!=ret.second; ++callSiteIt) {
                    auto ci = (*callSiteIt).second;
                    if (!isFromAlloc(ci->getArgOperand(arg->getArgNo())))
                        return false;
                }
                return true;
            }

            return false;
        }

        virtual bool runOnModule(Module &M);
        virtual void getAnalysisUsage(AnalysisUsage &AU) const {
        }

        Function* trace_read;
        Function* trace_write;
        int seq;
    };
}

char TraceMem::TraceMemPass::ID = 0;
static RegisterPass<TraceMem::TraceMemPass> X("TraceMem", "TraceMem World Pass");

TraceMem::TraceMemPass::TraceMemPass() : ModulePass(ID) {
}

TraceMem::TraceMemPass::~TraceMemPass() {
}

bool TraceMem::TraceMemPass::analyzeFunction(Function * const function) {
    bool memory_dependancies = false;
    for (inst_iterator instr_i = inst_begin(function), E = inst_end(function); instr_i != E; ++instr_i) {
        Instruction *I = &*instr_i;
        fAnalyzedInstructions.insert(I);

        if (LoadInst *li = dyn_cast<LoadInst>(I)) {
            if (!isFromAlloc(li->getPointerOperand())) {
                ++num_loads;
                const Type* type = li->getType();

                if (type->isIntegerTy() && (type->getIntegerBitWidth() == 32)) {
                    ++num_loads_instrumented;
                    Value * args[1];
                    args[0] = ConstantInt::get(IntegerType::get(li->getContext(), 32), seq++, true);
                    args[1] = li->getPointerOperand();
                    Instruction *newI = CallInst::Create(trace_read, ArrayRef<Value*>(args, 2), "");
                    ReplaceInstWithInst(li, newI);
                    instr_i = inst_begin(function);
                    while (&*instr_i != newI) {
                        ++instr_i;
                    }
                }
            }
        } else if (StoreInst *si = dyn_cast<StoreInst>(I)) {
            if (!isFromAlloc(si->getPointerOperand())) {
                ++num_stores;
                const Type* type = si->getValueOperand()->getType();

                if (type->isIntegerTy() && (type->getIntegerBitWidth() == 32)) {
                    ++num_stores_instrumented;
                    Value * args[2];
                    args[0] = ConstantInt::get(IntegerType::get(si->getContext(), 32), seq++, true);
                    args[1] = si->getPointerOperand();
                    args[2] = si->getValueOperand();
                    Instruction *newI = CallInst::Create(trace_write, ArrayRef<Value*>(args, 3), "");
                    ReplaceInstWithInst(si, newI);
                    instr_i = inst_begin(function);
                    while (&*instr_i != newI) {
                        ++instr_i;
                    }
                }
            }
            ++num_stores;
        } else if (CallInst *ci = dyn_cast<CallInst>(I)) {
            Function* called = ci->getCalledFunction();
            if (!called || !called->isIntrinsic()) {
                fFunctionCalls.insert(ci);
                if (called) {
                    if (!called->isIntrinsic()
                        && called->hasName()) {
                        fFunctionCallSites.insert(std::pair<Function*,CallInst*>(called, ci));
                    }
                }
            }
        }
    }

    return memory_dependancies;
}

bool TraceMem::TraceMemPass::runOnModule(Module &M) {
    FunctionSet fAdded;

    trace_read = dyn_cast<Function>(M.getOrInsertFunction("trace_readi32",
            Type::getInt32Ty(M.getContext()),
            Type::getInt32Ty(M.getContext()),
            Type::getInt32PtrTy(M.getContext()),
            NULL));
    trace_write = dyn_cast<Function>(M.getOrInsertFunction("trace_writei32",
            Type::getVoidTy(M.getContext()),
            Type::getInt32Ty(M.getContext()),
            Type::getInt32PtrTy(M.getContext()),
            Type::getInt32Ty(M.getContext()),
            NULL));

    seq = 0;

    // Process each function
    for (auto i = M.begin(), ie = M.end(); i != ie; ++i) {
        auto func = i;
        auto it = fAdded.find(func);
        if (it == fAdded.end()) {
            fAdded.insert(func);

            if (func->size()) {
                analyzeFunction(func);
                ++num_functions_analyzed;
            } else {
                ++num_functions_skipped;
            }
        }
    }

    num_instructions_analyzed = fAnalyzedInstructions.size();

    return true;
}
