#ifndef CASCMD_H
#define CASCMD_H

#include <casdef.h>
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


extern "C" {
	static Tcl_ThreadCreateProc EpicsEventLoop;
}

class AsynPV;

// server instance
class AsynServer : public TclClass {
public:
    AsynServer (ClientData cdata, Tcl_Interp *interp, int objc, Tcl_Obj * const objv[]);
    ~AsynServer();

	int createPV(int objc, Tcl_Obj * const objv[]);
	int findPV(int objc, Tcl_Obj * const objv[]);
	int listPV(int objc, Tcl_Obj * const objv[]);

	void removePV(std::string name);

	Tcl_ThreadId mainid;
	std::unordered_map<std::string, AsynPV*> PVs;
	bool alive;
};

TCLCLASSDECLARE(AsynServer)

// server instance
class AsynPV : public TclClass {
public:
    AsynPV(AsynServer &server, std::string PVname, aitEnum type, unsigned int count);
    ~AsynPV();

	int write(int objc, Tcl_Obj * const objv[]);
	int read(int objc, Tcl_Obj * const objv[]);
	int name(int objc, Tcl_Obj * const objv[]);
	
	AsynServer &server;
	const std::string PVname;
	const aitEnum type;
	const unsigned int count;

};

TCLCLASSDECLAREEXPLICIT(AsynPV)
#endif
