#ifndef CASCMD_H
#define CASCMD_H

#include <casdef.h>
#include <gddAppFuncTable.h>
#include <gddApps.h>
#include <epicsTime.h>
#include <epicsTimer.h>
#include <smartGDDPointer.h>
#include <caEventMask.h>
#include <fdManager.h>
#undef INLINE /* conflicting definition from Tcl and EPICS */
#include <tcl.h>
#include "casExport.h"

#include "tclclass.h"

#include <unordered_map>
#include <string>


struct GetRequestEvent {
	Tcl_Event ev;
	casPV *PV;
};


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
	Tcl_ThreadId mainid;
	std::unordered_map<std::string, AsynPV*> PVs;
	bool alive;
	/* std::atomic_bool activate; */
public:
    AsynServer (ClientData cdata, Tcl_Interp *interp, int objc, Tcl_Obj * const objv[]);
    ~AsynServer();

	// Tcl interface functions
	int createPV(int objc, Tcl_Obj * const objv[]);
	int findPV(int objc, Tcl_Obj * const objv[]);
	int listPV(int objc, Tcl_Obj * const objv[]);
	
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
 
};


// PV instance
class AsynPV : public TclClass {
public:
    AsynPV(AsynServer &server, std::string PVname, aitEnum type, unsigned int count);
    ~AsynPV();
	
	/* write and read from perspective of the server */
	int write(int objc, Tcl_Obj * const objv[]);
	int read(int objc, Tcl_Obj * const objv[]);
	int name(int objc, Tcl_Obj * const objv[]);
	
	int readcommand(int objc, Tcl_Obj * const objv[]);
	int writecommand(int objc, Tcl_Obj * const objv[]);
	int lowlimit(int objc, Tcl_Obj * const objv[]);
	int highlimit(int objc, Tcl_Obj * const objv[]);
	int precision(int objc, Tcl_Obj * const objv[]);
	int units(int objc, Tcl_Obj * const objv[]);

	int commandfun(int objc, Tcl_Obj * const objv[], Tcl_Obj* &prefix);
	
	/* publish event if it changes */
	void postUpdateEvent();

	const std::string getName() const { return rawPV.PVname; }
	
	AsynServer &server;	
	AsynCasPV rawPV;

	Tcl_Obj *readCmdPrefix;
	Tcl_Obj *writeCmdPrefix;
};

TCL_DECLARE_MUTEX(CmdMutex);

template <typename CType> Tcl_Obj* NewTclObj(CType value);

template <> Tcl_Obj* NewTclObj(double value) { return Tcl_NewDoubleObj(value); }
template <> Tcl_Obj* NewTclObj(unsigned int value) { return Tcl_NewWideIntObj(value); }
template <> Tcl_Obj* NewTclObj(std::string value) { return Tcl_NewStringObj(value.c_str(), -1); }


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
		Tcl_SetObjResult(interp, NewTclObj<CType>(prop));

		Tcl_MutexUnlock(&CmdMutex);
		return TCL_OK;
	}

	/* set value */
	Tcl_Obj *value = objv[2];
	
	int code = FromTclObj<CType>(interp, value, prop);
	Tcl_MutexUnlock(&CmdMutex);
	return code;
}


TCLCLASSDECLAREEXPLICIT(AsynPV)
#endif
