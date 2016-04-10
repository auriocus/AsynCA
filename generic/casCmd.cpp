/*
 * caCmd.c --
 */

#include "casCmd.hpp"


/* Create a new process variable object and return it
 * The callback is invoked for every change of the connection status */

int startServerCmd(ClientData clientData, Tcl_Interp *interp, int objc, Tcl_Obj * const objv[]) {

	/* Start thread with the EPICS event loop */
	static Tcl_ThreadId id = NULL;    /* holds identity of thread created */

	int code = AsynServerCreateCmd(clientData, interp, objc, objv);
	AsynCasPV::initFT(); /* initialize dispatch table */
	
	if (code != TCL_OK) { return TCL_ERROR; }

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

	return TCL_OK;
}

static Tcl_ThreadCreateType EpicsEventLoop (ClientData clientData)
{
	//Tcl_ThreadId mainid = static_cast<Tcl_ThreadId> (clientData);
	/* Run epics select loop */
	fileDescriptorManager.process(1.0);
	while (true) {
		// printf("Boing \n");
		fileDescriptorManager.process(100.0);
	}
	TCL_THREAD_CREATE_RETURN;
}

PipeObject::PipeObject() {
	int fd[2];
	if (pipe(fd)==-1) {
		printf("Error creating pipe - hm :(\n");
	}
	readfd=fd[0];
	writefd=fd[1];
}

PipeObject::~PipeObject() {
	close(writefd);
	close(readfd);
}

wakeupEpicsLoopFD::wakeupEpicsLoopFD() :
	PipeObject(), fdReg(readfd, fdrRead, false)
{
	/* create a pipe */

}

void wakeupEpicsLoopFD::callBack() {
	char msg;
	int bytesread = read(readfd, &msg, 1);
	//printf("Loop woken up: %c\n", msg);
}

void wakeupEpicsLoopFD::send(char msg) {
	write(writefd, &msg, 1);
}


TCLCLASSIMPLEMENT(AsynServer, createPV, findPV, listPV);

// server instance
AsynServer::AsynServer(ClientData cdata, Tcl_Interp *interp, int objc, Tcl_Obj * const objv[]) :
	alive(true)
{
	if (objc != 1) {
		Tcl_WrongNumArgs(interp, 1, objv, "");
		throw(TCL_ERROR);
	}
	mainid = Tcl_GetCurrentThread();
	setDebugLevel(10);
	alertfd = new wakeupEpicsLoopFD();
#if 0
	/* run an event loop for 10s */
	for (int t=0; t<10; t++) {
		fileDescriptorManager.process(1000.0);
		printf("Boing \n");
	}
#endif
}	

AsynServer::~AsynServer() { 
	/* destroy all PVs from the map */
	alive = false;
	for (auto &p: PVs) {
		delete p.second;
	}

	delete alertfd;
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
		Tcl_SetObjResult(interp, string2Tcl(pvit->second->getName()));
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

// More advanced pvExistTest() isnt needed so we forward to
// original version. This avoids sun pro warnings and speeds 
// up execution.
//
pvExistReturn AsynServer::pvExistTest (const casCtx & ctx, const caNetAddr &, const char * pPVName)
{
	return pvExistTest ( ctx, pPVName );
}

pvExistReturn AsynServer::pvExistTest
    ( const casCtx& ctxIn, const char * pPVName )
{
   	auto pvit = PVs.find(pPVName);
	if (pvit == PVs.end()) {
        return pverDoesNotExistHere;
	} else {
        return pverExistsHere;
	}
}

pvAttachReturn AsynServer::pvAttach(const casCtx &, const char * pPVName ) {
	auto pvit = PVs.find(pPVName);
	if (pvit == PVs.end()) {
        return S_casApp_pvNotFound;
	} else {
        return pvit->second->rawPV;
	}
}

void  AsynServer::wakeup() {
	alertfd->send();
}

TCLCLASSIMPLEMENTEXPLICIT(AsynPV, read, write, name);

AsynPV::AsynPV(AsynServer &server, std::string name, aitEnum type, unsigned int count) : 
	server(server), rawPV(*this, name, type, count) {
}

AsynPV::~AsynPV() {
	server.removePV(rawPV.PVname);
}

int AsynPV::read(int objc, Tcl_Obj * const objv[]) {
	Tcl_SetObjResult(interp, Tcl_NewStringObj("read", -1));
	return TCL_OK;
}

int AsynPV::write(int objc, Tcl_Obj * const objv[]) {
	if (objc != 3) {
		Tcl_WrongNumArgs(interp, 2, objv, "value");
		return TCL_ERROR;
	}
	
	if (rawPV.count != 1) {
		Tcl_SetObjResult(interp, Tcl_ObjPrintf("Unimplemented vector put for %d items, must be 1", rawPV.count));
		return TCL_ERROR;
	}

	if (rawPV.type != aitEnumFloat64) {
		Tcl_SetObjResult(interp, Tcl_ObjPrintf("Unimplemented data type %d, must be %d (double)", rawPV.type, aitEnumFloat64));
		return TCL_ERROR;
	}

	double val;
	if (Tcl_GetDoubleFromObj(interp, objv[2], &val) != TCL_OK) {
		return TCL_ERROR;
	}
	/* finally put it and update the time stamp */
	rawPV.data->put(val);
	
	aitTimeStamp gddts(epicsTime::getCurrent());
	rawPV.data->setTimeStamp(&gddts);
	
	/* postEvent, needed for camonitor */
    casEventMask select ( server.valueEventMask() | server.logEventMask() );
	rawPV.postEvent(select, *rawPV.data);
	/* postEvent does not wake up the fileDescriptorManager event loop 
	 * Ask the server to activate the wakeup socket */
	server.wakeup();
	return TCL_OK;
}

int AsynPV::name(int obj, Tcl_Obj * const objv[]) {
	Tcl_SetObjResult(interp, string2Tcl(rawPV.PVname));
	return TCL_OK;
}
	

AsynCasPV::AsynCasPV(AsynPV &asynPV, std::string PVname, aitEnum type, unsigned int count) :
	asynPV(asynPV), PVname(PVname), type(type), count(count), 
	highlimit(10.0), lowlimit(0.0), precision(6), units("mm") 
{
	data = new gddScalar(gddAppType_value, type);
	data->put(3.14159);
}


/* Dispatch table */
bool AsynCasPV::hasBeenInitialized = false;
gddAppFuncTable<AsynCasPV> AsynCasPV::ft;

void AsynCasPV::initFT ()
{
    if ( AsynCasPV::hasBeenInitialized ) {
            return;
    }

    //
    // time stamp, status, and severity are extracted from the
    // GDD associated with the "value" application type.
    //
    AsynCasPV::ft.installReadFunc ("value", &AsynCasPV::getValue);
    AsynCasPV::ft.installReadFunc ("precision", &AsynCasPV::getPrecision);
    AsynCasPV::ft.installReadFunc ("graphicHigh", &AsynCasPV::getHighLimit);
    AsynCasPV::ft.installReadFunc ("graphicLow", &AsynCasPV::getLowLimit);
    AsynCasPV::ft.installReadFunc ("controlHigh", &AsynCasPV::getHighLimit);
    AsynCasPV::ft.installReadFunc ("controlLow", &AsynCasPV::getLowLimit);
    AsynCasPV::ft.installReadFunc ("alarmHigh", &AsynCasPV::getHighLimit);
    AsynCasPV::ft.installReadFunc ("alarmLow", &AsynCasPV::getLowLimit);
    AsynCasPV::ft.installReadFunc ("alarmHighWarning", &AsynCasPV::getHighLimit);
    AsynCasPV::ft.installReadFunc ("alarmLowWarning", &AsynCasPV::getLowLimit);
    AsynCasPV::ft.installReadFunc ("units", &AsynCasPV::getUnits);
	AsynCasPV::ft.installReadFunc ("enums", &AsynCasPV::getEnums);

    AsynCasPV::hasBeenInitialized = true;
}


caStatus AsynCasPV::getPrecision ( gdd & prec )
{
    prec.put(precision);
    return S_cas_success;
}

caStatus AsynCasPV::getHighLimit ( gdd & value )
{
    value.put(highlimit);
    return S_cas_success;
}

caStatus AsynCasPV::getLowLimit ( gdd & value )
{
    value.put(lowlimit);
    return S_cas_success;
}

caStatus AsynCasPV::getUnits( gdd & unitstring )
{
	aitString str(units.c_str());

    unitstring.put(str);
    return S_cas_success;
}

//
// exPV::getEnums()
//
// returns the eneumerated state strings
// for a discrete channel
//
// The PVs in this example are purely analog,
// and therefore this isnt appropriate in an
// analog context ...
//
caStatus AsynCasPV::getEnums ( gdd & enumsIn )
{
    if ( type == aitEnumEnum16 ) {
        return S_cas_success;
    }

    return S_cas_success;
}

caStatus AsynCasPV::getValue ( gdd & value )
{
    caStatus status;

    if (data.valid()) {
        gddStatus gdds;

        gdds = gddApplicationTypeTable::app_table.smartCopy ( &value, & (*this->data) );
        if (gdds) {
            status = S_cas_noConvert;   
        }
        else {
            status = S_cas_success;
        }
    }
    else {
        status = S_casApp_undefined;
    }
    return status;
}

const char *AsynCasPV::getName() const {
	return PVname.c_str();
}


aitEnum AsynCasPV::bestExternalType () const
{ return type; }



unsigned AsynCasPV::maxDimension() const {
	if (count==1) { return 0; }
	return 1;
}

aitIndex AsynCasPV::maxBound (unsigned dimension) const {
	if (dimension == 0) return count;
	return 0;
}	

caStatus AsynCasPV::read ( const casCtx &, gdd & protoIn )
{
    return ft.read ( *this, protoIn );
}

caStatus AsynCasPV::write ( const casCtx &, const gdd & valueIn ) 
{
	/* a write is silently ignored for now */
	return S_casApp_success;
}

