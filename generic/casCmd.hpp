#ifndef CASCMD_H
#define CASCMD_H

#include <casdef.h>
#include <gddAppFuncTable.h>
#include <gddApps.h>
#include <epicsTime.h>
#include <epicsTimer.h>
#include <smartGDDPointer.h>
#include <caeventmask.h>
#include <fdManager.h>
#include <db_access.h>

#undef INLINE /* conflicting definition from Tcl and EPICS */
#include <tcl.h>
#include "casExport.h"

#include "tclclass.h"

#include <unordered_map>
#include <string>
#include <vector>



/* utility to convert a std::string into a new Tcl_Obj */

static inline Tcl_Obj * string2Tcl(std::string s) {
	return Tcl_NewStringObj(s.c_str(), s.length());
}

static inline void DecrIfNotNull(Tcl_Obj*& o) {
	if (o) {
		Tcl_DecrRefCount(o);
		o=NULL;
	}
}

//static int GetGddFromTclObj(Tcl_Interp *interp, Tcl_Obj *value, gdd & storage);
static Tcl_Obj* NewTclObjFromGdd(const gdd & value);

extern "C" {
	static Tcl_ThreadCreateProc EpicsEventLoop;
}

class PipeObject {
public:
	int writefd;
	int readfd;
	PipeObject();
	~PipeObject();
};


class wakeupEpicsLoopFD : public PipeObject, public fdReg {
public:	
	wakeupEpicsLoopFD();
	void callBack();
	void send(char msg='w');
};

class AsynPV;

// server instance
class AsynServer : public TclClass, public caServer {
public:
	Tcl_ThreadId mainid;
	Tcl_ThreadId epicsloopid;
	std::unordered_map<std::string, AsynPV*> PVs;
	bool alive;
	
    AsynServer (ClientData cdata, Tcl_Interp *interp, int objc, Tcl_Obj * const objv[]);
    ~AsynServer();

	// Tcl interface functions
	int createPV_(int objc, Tcl_Obj * const objv[]);
	int findPV_(int objc, Tcl_Obj * const objv[]);
	int listPV_(int objc, Tcl_Obj * const objv[]);
	
	// Notifier from PV, if it is destroyed by Tcl
	void removePV(std::string name);
	
	// EPICS interface functions
	pvExistReturn pvExistTest(const casCtx& ctxIn, const char * pPVName);
	pvExistReturn pvExistTest(const casCtx & ctx, const caNetAddr &, const char * pPVName);
    pvAttachReturn pvAttach(const casCtx &, const char * pPVName );
    
	// tell the event loop to process the new asynchronous events
	void wakeup();

	wakeupEpicsLoopFD * alertfd;
};

TCLCLASSDECLARE(AsynServer)

class AsynPV;

// PV object to return to the CAS library
class AsynCasPV : public casPV {
public:
	AsynCasPV(AsynPV &asynPV, std::string PVname, aitEnum type, unsigned int count);
	
	/* back reference to Tcl object */
	AsynPV &asynPV;
	
	const std::string PVname;
	const aitEnum type;
	const unsigned int count;
	double lowlimit;
	double highlimit;
	unsigned int precision;
	std::string units;
	
	std::vector<std::string> enumstrings;
	std::unordered_map<std::string, aitEnum16> enummap;
	
	/* the data store - should be a gdd polymorphic type in the end */
    smartGDDPointer data;
	
	// Overloads from CAS server library
	const char * getName() const;
	aitEnum bestExternalType () const;

	caStatus read ( const casCtx &, gdd & protoIn );
	caStatus write ( const casCtx &, const gdd & valueIn );
	
	// don't kill this PV from the server library
	void destroy() { }    
	
	gddAppFuncTableStatus getPrecision(gdd &value);
    gddAppFuncTableStatus getHighLimit(gdd &value);
    gddAppFuncTableStatus getLowLimit(gdd &value);
    gddAppFuncTableStatus getUnits(gdd &value);
    gddAppFuncTableStatus getValue(gdd &value);
    gddAppFuncTableStatus getEnums(gdd &value);

    // return vector size of this
	// process variable
	unsigned maxDimension() const;
    aitIndex maxBound (unsigned dimension) const;
    //
    // static
    //
    static gddAppFuncTable<AsynCasPV> ft;
    static bool hasBeenInitialized;
	static void initFT();

	int putTclObj(Tcl_Interp *interp, Tcl_Obj *value);
	
};


// PV instance
class AsynPV : public TclClass {
public:
    AsynPV(AsynServer &server, std::string PVname, aitEnum type, unsigned int count);
    ~AsynPV();
	
	/* write and read from perspective of the server */
	int write_(int objc, Tcl_Obj * const objv[]);
	int read_(int objc, Tcl_Obj * const objv[]);
	int readenum_(int objc, Tcl_Obj * const objv[]);
	int enumstrings_(int objc, Tcl_Obj * const objv[]);
	int name_(int objc, Tcl_Obj * const objv[]);
	
	int readcommand_(int objc, Tcl_Obj * const objv[]);
	int writecommand_(int objc, Tcl_Obj * const objv[]);
	int lowlimit_(int objc, Tcl_Obj * const objv[]);
	int highlimit_(int objc, Tcl_Obj * const objv[]);
	int precision_(int objc, Tcl_Obj * const objv[]);
	int units_(int objc, Tcl_Obj * const objv[]);

	int commandfun(int objc, Tcl_Obj * const objv[], Tcl_Obj* &prefix);
	
	/* publish event if it changes */
	void postUpdateEvent();

	const std::string getName() const { return rawPV.PVname; }
	
	AsynServer &server;	
	AsynCasPV rawPV;

	Tcl_Obj *readCmdPrefix;
	Tcl_Obj *writeCmdPrefix;
};

TCLCLASSDECLAREEXPLICIT(AsynPV)

TCL_DECLARE_MUTEX(CmdMutex);

inline Tcl_Obj* NewTclObj(double value) { return Tcl_NewDoubleObj(value); }
inline Tcl_Obj* NewTclObj(int value) { return Tcl_NewWideIntObj(value); }
inline Tcl_Obj* NewTclObj(unsigned int value) { return Tcl_NewWideIntObj(value); }
inline Tcl_Obj* NewTclObj(long value) { return Tcl_NewWideIntObj(value); }
inline Tcl_Obj* NewTclObj(unsigned long value) { return Tcl_NewWideIntObj(value); }
inline Tcl_Obj* NewTclObj(const std::string &value) { return Tcl_NewStringObj(value.c_str(), value.size()); }


template <typename CType> int FromTclObj(Tcl_Interp * interp, Tcl_Obj* value, CType &out);
template <> int FromTclObj(Tcl_Interp * interp, Tcl_Obj* value, double &out) { 
	return Tcl_GetDoubleFromObj(interp, value, &out);
}

template <> int FromTclObj(Tcl_Interp * interp, Tcl_Obj* value, unsigned int &out) {
	Tcl_WideInt v;
	if (Tcl_GetWideIntFromObj(interp, value, &v) != TCL_OK) { return TCL_ERROR; }
	out = v;
	if (out != v) {
		Tcl_SetObjResult(interp, Tcl_NewStringObj("Value out of range", -1));
		return TCL_ERROR;
	}
	return TCL_OK;
}

template <> int FromTclObj(Tcl_Interp * interp, Tcl_Obj* value, std::string &out) { 
	out = Tcl_GetString(value);
	return TCL_OK;
}

template <typename CType> 
int property(Tcl_Interp *interp, int objc, Tcl_Obj * const objv[], CType & prop, const char *desc) {
	if (objc != 2 && objc != 3) {
		Tcl_WrongNumArgs(interp, 2, objv, desc);
		return TCL_ERROR;
	}

	Tcl_MutexLock(&CmdMutex);
	
	/* get value  */
	if (objc == 2) {
		Tcl_SetObjResult(interp, NewTclObj(prop));

		Tcl_MutexUnlock(&CmdMutex);
		return TCL_OK;
	}

	/* set value */
	Tcl_Obj *value = objv[2];
	
	int code = FromTclObj<CType>(interp, value, prop);
	Tcl_MutexUnlock(&CmdMutex);
	return code;
}

class AsynCAReadRequest;

class AsynCARawReadRequest : public casAsyncReadIO {
public:
	AsynCARawReadRequest(AsynCAReadRequest &boxedrequest, const casCtx & ctx);
	~AsynCARawReadRequest();
	
	AsynCAReadRequest & boxedrequest;
	bool completed;
};


class AsynCAReadRequest : public TclClass {
public:
    AsynCAReadRequest (AsynPV &pv, const casCtx & ctx, gdd & protoIn);
	/* Runs in Tcl thread after construction, to make it complete */
	void callscript();
    
	virtual ~AsynCAReadRequest ();
	int return_ (int objc, Tcl_Obj *const objv[]);

	void droppedrequest();
	
	AsynCARawReadRequest *rawRequest;
    AsynPV & pv;
	bool completed;
	smartGDDPointer proto;
};

TCLCLASSDECLAREEXPLICIT(AsynCAReadRequest)

extern "C" int readRequestInvoke(Tcl_Event *p, int flags);

struct ReadRequestEvent {
	Tcl_Event ev;
	AsynCAReadRequest *request;
	Tcl_Interp *interp;
};

class AsynCAWriteRequest;

class AsynCARawWriteRequest : public casAsyncWriteIO {
public:
	AsynCARawWriteRequest(AsynCAWriteRequest &boxedrequest, const casCtx & ctx);
	~AsynCARawWriteRequest();
	
	AsynCAWriteRequest & boxedrequest;
	bool completed;
};


class AsynCAWriteRequest : public TclClass {
public:
    AsynCAWriteRequest (AsynPV &pv, const casCtx & ctx, const gdd & gddvalue);
	/* Runs in Tcl thread after construction, to make it complete */
	void callscript();
    
	virtual ~AsynCAWriteRequest ();
	int return_ (int objc, Tcl_Obj *const objv[]);
	int value_ (int objc, Tcl_Obj *const objv[]);
	
	void droppedrequest();

	AsynCARawWriteRequest *rawRequest;
    AsynPV & pv;
	bool completed;
	Tcl_Obj *data;
};

TCLCLASSDECLAREEXPLICIT(AsynCAWriteRequest)

struct WriteRequestEvent {
	Tcl_Event ev;
	AsynCAWriteRequest *request;
	Tcl_Interp *interp;
};

extern "C" int writeRequestInvoke(Tcl_Event *p, int flags);
#endif
