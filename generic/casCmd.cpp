/*
 * caCmd.c --
 */

#include "casCmd.hpp"
#include <fdManager.h>


/* Create a new process variable object and return it
 * The callback is invoked for every change of the connection status */

int startServerCmd(ClientData clientData, Tcl_Interp *interp, int objc, Tcl_Obj * const objv[]) {

	/* Start thread with the EPICS event loop */
	static Tcl_ThreadId id = NULL;    /* holds identity of thread created */

	/* Run a singleton EPICS event thread, if the first server is started */
	if (id == NULL) {
		if (Tcl_CreateThread(&id, EpicsEventLoop, static_cast<ClientData>(NULL),
					TCL_THREAD_STACK_DEFAULT,
					TCL_THREAD_NOFLAGS) != TCL_OK) {
			/* Thread did not create correctly */
			Tcl_SetObjResult(interp, Tcl_NewStringObj("Couldn't initialize EPICS event loop", -1));
			return TCL_ERROR;
		}
	}
	/* All cleaned up nicely */
	return AsynServerCreateCmd(clientData, interp, objc, objv);
}

static Tcl_ThreadCreateType EpicsEventLoop (ClientData clientData)
{
	//Tcl_ThreadId mainid = static_cast<Tcl_ThreadId> (clientData);
	/* Run epics select loop */
	while (true) {
		fileDescriptorManager.process(1000.0);
	}
	TCL_THREAD_CREATE_RETURN;
}

TCLCLASSIMPLEMENT(AsynServer, createPV, findPV, listPV);

// server instance
AsynServer::AsynServer (ClientData cdata, Tcl_Interp *interp, int objc, Tcl_Obj * const objv[]) :
	alive(true)
{
	if (objc != 1) {
		Tcl_WrongNumArgs(interp, 1, objv, "");
		throw(TCL_ERROR);
	}
	mainid = Tcl_GetCurrentThread();
}

AsynServer::~AsynServer() { 
	/* destroy all PVs from the map */
	alive = false;
	for (auto &p: PVs) {
		delete p.second;
	}

	printf("AsynServer dying!\n"); 
}

int AsynServer::createPV(int objc, Tcl_Obj * const objv[]) {
	/* create a scalar double PV - TODO support all data types */
	if (objc < 3 || objc > 5) {
		Tcl_WrongNumArgs(interp, 1, objv, "name ?type ?count??");
		return TCL_ERROR;
	}
	std::string name = Tcl_GetString(objv[2]);
	/* check that this PV does not yet exist */
	if (PVs.find(name) != PVs.end()) {
		Tcl_SetObjResult(interp, Tcl_ObjPrintf("Process variable %s already exists in this server", name.c_str()));
		return TCL_ERROR;
	}


	aitEnum type = aitEnumFloat64;
	if (objc == 4) {
		/* if (Tcl_GetIndexFromObjStruct(interp, objv[3], \
			DataTypeTable, sizeof(DataTypeEntry), \
			"EPICS data type", 0, &index) != TCL_OK) */
		if (std::string(Tcl_GetString(objv[3])) != "double") {
			Tcl_SetObjResult(interp, Tcl_ObjPrintf("Wrong data type %s, only double implemented", Tcl_GetString(objv[3])));
			return TCL_ERROR;
		}
	}
	
	int count = 1;
	if (objc == 5) {
		if (Tcl_GetIntFromObj(interp, objv[4], &count) != TCL_OK) {
			return TCL_ERROR;
		}
		if (count<1) {
			Tcl_SetObjResult(interp, Tcl_ObjPrintf("Wrong count %d, must be >1", count));
			return TCL_ERROR;
		}
	}

	/* create PV */
	AsynPV *pv = new AsynPV(*this, name, type, count);
	PVs[name] = pv;
	return AsynPVLink(interp, pv);
}

int AsynServer::findPV(int objc, Tcl_Obj * const objv[]) {
	if (objc != 2) {
		Tcl_WrongNumArgs(interp, 1, objv, "name");
		return TCL_ERROR;
	}

	auto pvit = PVs.find(std::string(Tcl_GetString(objv[1])));
	if (pvit != PVs.end()) {
		Tcl_SetObjResult(interp, string2Tcl(pvit->second->PVname));
	}
	return TCL_OK;
}

int AsynServer::listPV(int objc, Tcl_Obj * const objv[]) {
	/* return all PVs as Tcl objects */
	Tcl_Obj *result = Tcl_NewObj();
	for (auto& p: PVs) {
		Tcl_DictObjPut(NULL, result, string2Tcl(p.first), p.second->GetCommandFullName());
	}
	Tcl_SetObjResult(interp, result);
	return TCL_OK;
}


void AsynServer::removePV(std::string name) {
	if (alive) {
		PVs.erase(name);
	}
}

TCLCLASSIMPLEMENTEXPLICIT(AsynPV, read, write, name);

AsynPV::AsynPV(AsynServer &server, std::string name, aitEnum type, unsigned int count) : 
	server(server), PVname(name),
	type(type), count(count) {
}

AsynPV::~AsynPV() {
	server.removePV(PVname);
}

int AsynPV::read(int objc, Tcl_Obj * const objv[]) {
	Tcl_SetObjResult(interp, Tcl_NewStringObj("read", -1));
	return TCL_OK;
}

int AsynPV::write(int objc, Tcl_Obj * const objv[]) {
	Tcl_SetObjResult(interp, Tcl_NewStringObj("write", -1));
	/* postIOnotify */
	return TCL_OK;
}

int AsynPV::name(int obj, Tcl_Obj * const objv[]) {
	Tcl_SetObjResult(interp, string2Tcl(PVname));
	return TCL_OK;
}
	


