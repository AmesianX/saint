#include <llvm/Pass.h>
#include <llvm/IR/Module.h>
#include <llvm/Analysis/LoopInfo.h>
#include <llvm/Support/InstIterator.h>
#include <llvm/IR/Instruction.h>
#include <llvm/Analysis/CallGraph.h>
#include <llvm/InstVisitor.h>
#include <llvm/Target/Mangler.h>

#include <fstream>

#include <vector>
#include <string>
using std::vector;
using std::string;

#include <set>
using std::set;

#include <map>
using std::map;

using std::pair;

using namespace llvm;

namespace {

#define ENTRY_POINT "main"

//Data structure representing the analysis flowset data type
//TODO: use StringMap from llvm
typedef map<Instruction *, set<Value*> >  FlowSet;
typedef map<Instruction *, set<Value*> >::iterator ItFlowSet;
typedef map<Instruction *, set<Value *> >::value_type ValTypeFlowSet;

typedef vector< pair<bool, string> > FunctionParam;

struct CTaintAnalysis : public ModulePass,
						public InstVisitor<CTaintAnalysis> {
	static char ID;

	CTaintAnalysis();
	void getAnalysisUsage(AnalysisUsage & AU) const;

	virtual bool runOnModule(Module & F);

	void visitLoadInst(LoadInst &I);
	void visitStoreInst(StoreInst &I);
	void visitGetElementPtrInst(GetElementPtrInst &I);

	/**
	 * Only executed during interprodural analysis
	 */
	void visitCallInst(CallInst &I);

private:
	const static string _taintId;
	const static string _taintSourceFile;
	static vector<string> _taintSources;

	void readTaintSourceConfig();

	/** Has the intraprocedural analysis been run */
	bool _intraFlag;

	/** Has the interprocedural Context-Insenstive analysis been run */
	bool _interFlag;

	/** Has the interprocedural Context-Senstive analysis been run */
	bool _interContextSensitiveFlag;

	/** Pointer to the 'main' function */
	Function *_pointerMain;

	/** Pointer the 'main' function's first instruction */
	Instruction *_firstInstMain;

	CallGraphNode *_cgRootNode;

	/**
	 * Map from program funtion signatures as string to
	 * Function pointers
	 */
	map<string, Function*> _signatureToFunc;
	typedef map<string, Function*>::iterator ItFunction;

	/**
	 * Summary table where we store function parameters and
	 * return value taunt information
	 */
	map<string, FunctionParam> _summaryTable;
	typedef map<string, FunctionParam>::iterator itSummaryTable;

	FlowSet _IN;
	FlowSet _OUT;

	/**
	 * To access _IN values
	 */
	set<Value*> * getInFlow(Instruction *);

	/**
	 * To access _OUT values
	 */
	set<Value*> * getOutFlow(Instruction *);

	void addInFlowSet(FlowSet &aFlowSet, Instruction *aInst, Value *aValue);

	void addInFlow(Instruction *, Value *aValue);

	void addOutFlow(Instruction *, Value *aValue);

	inline static void log(const string &msg) {
		errs() << _taintId << msg << '\n';
	}

	void initDataFlowSet(Function &f);

	void intraFlow();
	void interFlow(Function *caller, Instruction &inst);
};

const string CTaintAnalysis::_taintId("[STTAL]");

const string CTaintAnalysis::_taintSourceFile("cfg/sources.cfg");

vector<string> CTaintAnalysis::_taintSources;

void CTaintAnalysis::readTaintSourceConfig() {
	//Open the file with taint source functions listed
	std::ifstream srcFile(_taintSourceFile.c_str());
	string aSource;

	//TODO: use the mangler here Mangler aMangler

	while (!srcFile.eof()) {
		std::getline(srcFile, aSource);
		if (!aSource.empty()) {
			_taintSources.push_back("__isoc99_"+aSource);
			log("read taint source: " + aSource);
		}
	}

	srcFile.close();
}

char CTaintAnalysis::ID = 0;


CTaintAnalysis::CTaintAnalysis() : ModulePass(ID) {
	_pointerMain = 0;
	_firstInstMain = 0;
	_intraFlag = false;
	_interFlag = false;
	_interContextSensitiveFlag = false;
	readTaintSourceConfig();
}

void CTaintAnalysis::getAnalysisUsage(AnalysisUsage &AU) const {
	AU.setPreservesAll();
	AU.addRequired<LoopInfo > ();
	AU.addRequired<CallGraph > ();
}

set<Value*> * CTaintAnalysis::getInFlow(Instruction *aInst) {

	ItFlowSet itIN = _IN.find(aInst);

	if (itIN != _IN.end())
		return &itIN->second;

	return 0;
}

set<Value*> * CTaintAnalysis::getOutFlow(Instruction *aInst) {

	ItFlowSet itIN = _OUT.find(aInst);

	if (itIN != _OUT.end())
		return &itIN->second;

	return 0;
}

void CTaintAnalysis::addInFlowSet(FlowSet &aFlowSet, Instruction *aInst, Value *aValue) {
	ItFlowSet curValues = aFlowSet.find(aInst);

	if (curValues != aFlowSet.end()) {
		set<Value *> taintedValues = curValues->second;
		taintedValues.insert(aValue);
	}
	else {
		set<Value *> taintedValues;
		taintedValues.insert(aValue);
		aFlowSet.insert( ValTypeFlowSet(aInst, taintedValues) );
	}
}

void CTaintAnalysis::addInFlow(Instruction *aInst, Value *aValue) {
	addInFlowSet(_IN, aInst, aValue);
}

void CTaintAnalysis::addOutFlow(Instruction *aInst, Value *aValue) {
	addInFlowSet(_OUT, aInst, aValue);
}

void CTaintAnalysis:: intraFlow() {
	if ( _intraFlag )
		return;

	Function *cur = 0;
	for(ItFunction f = _signatureToFunc.begin(); f != _signatureToFunc.end(); ++f) {
		cur = f->second;
		visit(cur);
	}
	_intraFlag = true;
}

void CTaintAnalysis::interFlow(Function *caller, Instruction &inst) {
	if (!_intraFlag) {
		intraFlow();
	}
}

bool CTaintAnalysis::runOnModule(Module &m) {
	log("module identifier is " + m.getModuleIdentifier());

	for (Module::iterator b = m.begin(), be = m.end(); b != be; ++b) {

		Function *f = dyn_cast<Function > (b);

		//We only handle function defined in the code
		if (f->isDeclaration())
			continue;

		string fName = f->getName().str();
		log("discovered function " + fName);

		//TODO: use function signature as key instead
		_signatureToFunc[fName] = f;

		if ( !_pointerMain && 0 == fName.compare(ENTRY_POINT) ) {
			_pointerMain = f;
			_firstInstMain = &*inst_begin(_pointerMain);
		}

		//Performs intraprocedural analysis at this point
		//visit(f);
	}

	intraFlow();

	return false;
}

void CTaintAnalysis::initDataFlowSet(Function &f){
	//map<Instruction, pair<Value, vector<Instruction> > >
	for (inst_iterator inst = inst_begin(f), end = inst_end(f); inst != end; ++inst) {
		//IN[*inst] =
	}
}

void CTaintAnalysis::visitLoadInst(LoadInst &inst)
{
	errs() << "a load inst" << "\n";
	inst.print(errs());
	errs() << "\n";
}

void CTaintAnalysis::visitStoreInst(StoreInst &inst)
{
	Value *val = inst.getValueOperand();

	if (val->getType()->isPointerTy()) {
		//COPY [p=q]
		set<Value*> * inQ = getInFlow(&inst);
		if (inQ && !inQ->empty()) {
			addOutFlow(&inst, val);
			errs() << "Adding an outflow" << "\n";
		}
	}
	else {
		//STORE [*p=q]
	}
	//errs() << "Type of " << val->getName().str()
	//		<< " is ";
	//val->getType()->print(errs());
}

void CTaintAnalysis::visitGetElementPtrInst(GetElementPtrInst &I)
{

}

/*
 * Interprocedural analysis
 */
void CTaintAnalysis::visitCallInst(CallInst & aCallInst)
{
	if (_intraFlag) {

	}
	else {
		//Intraprocedural analysis case: recognizing sources
		Function *callee = aCallInst.getCalledFunction();
		string calleeName = callee->getName().str();
		vector<string>::iterator result = std::find(_taintSources.begin(), _taintSources.end(), calleeName);
		if (result != _taintSources.end()) {

			log("found a taint source: " + calleeName);
		}

	}
}

static RegisterPass<CTaintAnalysis>
X("ctaintmod", "CTaint Module Pass",
		false /* Only looks at CFG */,
		true /* Analysis Pass */);

}
