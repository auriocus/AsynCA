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

static int GetGddFromTclObj(Tcl_Interp *interp, Tcl_Obj *value, gdd & storage) {
	/* Try to convert the Tcl_Obj into the data type of the gdd.
	 * Return a Tcl success code */
	
	if (storage.dimension() != 0) {
		if (interp)
			Tcl_SetObjResult(interp, Tcl_ObjPrintf("Unimplemented vector put for %d items, must be 1", storage.dimension()));
		return TCL_ERROR;
	}

	switch (storage.primitiveType()) {
		case aitEnumFloat64:
		case aitEnumFloat32: {
			double d;
			if (Tcl_GetDoubleFromObj(interp, value, &d) != TCL_OK) {
				return TCL_ERROR;
			}
			/* finally put it and update the time stamp */
			storage.putConvert(d);
			
			break;
		}
		
		#define INTVALCONV(AITENUM, AITYPE) \
			case AITENUM: { \
							 Tcl_WideInt val;\
							 if (Tcl_GetWideIntFromObj(interp, value, &val) != TCL_OK) {\
								 return TCL_ERROR;\
							 }\
							 AITYPE tval = val;\
							 \
							 if (tval != val) {\
								 /* value doesn't fit */\
								 Tcl_SetObjResult(interp, Tcl_NewStringObj("Value outside range for type " STRINGIFY(AITYPE), -1));\
								 return TCL_ERROR;\
							 }\
							 storage.put(tval); \
							 break; \
						 }

		INTVALCONV(aitEnumUint8, aitUint8)
		INTVALCONV(aitEnumInt8, aitInt8)
		INTVALCONV(aitEnumUint16, aitUint16)
		INTVALCONV(aitEnumInt16, aitInt16)
		INTVALCONV(aitEnumUint32, aitUint32)
		INTVALCONV(aitEnumInt32, aitInt32)
		INTVALCONV(aitEnumEnum16, aitEnum16)
		
#undef INTVALCONV
		case aitEnumFixedString: {
			int len;
			const char *bytes = Tcl_GetStringFromObj(value, &len);
			const int maxlen = sizeof(aitFixedString);
			if (len > maxlen) {
				Tcl_SetObjResult(interp, Tcl_ObjPrintf("String too long (%d bytes), max %d bytes allowed", len, maxlen));
				return TCL_ERROR;
			}
			
			
			storage.putConvert(bytes);
			break;
		}

		default: {
			if (interp)
				Tcl_SetObjResult(interp, Tcl_ObjPrintf("Unimplemented data type %d", storage.primitiveType()));
			return TCL_ERROR;
		}
	}

	aitTimeStamp gddts(epicsTime::getCurrent());
	storage.setTimeStamp(&gddts);
	return TCL_OK;
}

/* Convert the gdd object into an equivalent Tcl object */
static Tcl_Obj* NewTclObjFromGdd(const gdd & value) {
	if (value.dimension() != 0) {
		return	Tcl_ObjPrintf("Unimplemented vector data with %d items, must be 1", value.dimension());
	}

	switch (value.primitiveType()) {
		/* floating point types */
		case aitEnumFloat64:
		case aitEnumFloat32: {
			aitFloat64 val;
			value.getConvert(val);
			return Tcl_NewDoubleObj(val);
		}
		/* integer types, all are simply converted to Tcl_WideInt*/
		case aitEnumInt8:
		case aitEnumUint8:
		case aitEnumInt16:
		case aitEnumUint16:
		case aitEnumInt32:
		{
			aitInt32 val;
			value.getConvert(val);
			return Tcl_NewWideIntObj(val);
		}
		case aitEnumUint32: {
			aitUint32 val;
			value.getConvert(val);
			return Tcl_NewWideIntObj(val);
		}
		default: {
			return Tcl_ObjPrintf("Unimplemented data type %d items", value.primitiveType());
		}

	}
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
	//setDebugLevel(10);
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
	
	/* wake up the notifier to inform that the PVs are gone */
	wakeup();
	delete alertfd;
	printf("AsynServer dying!\n"); 
}


struct gddTypeItem {
	const char *typestring;
	aitEnum gddtype;
};

static gddTypeItem gddTypeTable[] = {
	{"double", aitEnumFloat64},
	{"float64", aitEnumFloat64},
	{"float", aitEnumFloat32},
	{"float32", aitEnumFloat32},
	{"int8", aitEnumInt8},
	{"uint8", aitEnumUint8},
	{"int16", aitEnumInt16},
	{"uint16", aitEnumUint16},
	{"int32", aitEnumInt32},
	{"uint32", aitEnumUint32},
	{"enum", aitEnumEnum16},
	{"string", aitEnumFixedString},
	{nullptr, aitEnumInvalid},
};


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
	if (objc >= 4) {
		int typeindex;
		if (Tcl_GetIndexFromObjStruct(interp, objv[3],
			gddTypeTable, sizeof(gddTypeItem), "EPICS data type", 0, &typeindex) != TCL_OK) {
			return TCL_ERROR;
		}
		type = gddTypeTable[typeindex].gddtype;
		printf("Data type: %d\n", type);
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
	Tcl_SetObjResult(interp, AsynPVAliasCreate(interp, pv));
	return TCL_OK;
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

TCLCLASSIMPLEMENTEXPLICIT(AsynPV, name, read, write, readcommand, writecommand, lowlimit, highlimit, precision, units);

AsynPV::AsynPV(AsynServer &server, std::string name, aitEnum type, unsigned int count) : 
	server(server), rawPV(*this, name, type, count),
	readCmdPrefix(NULL), writeCmdPrefix(NULL)
{

}

AsynPV::~AsynPV() {
	server.removePV(rawPV.PVname);
}

int AsynPV::read(int objc, Tcl_Obj * const objv[]) {
	aitFloat64 val;
	rawPV.data->getConvert(val);
	Tcl_SetObjResult(interp, Tcl_NewDoubleObj(val));
	return TCL_OK;
}

int AsynPV::write(int objc, Tcl_Obj * const objv[]) {
	if (objc != 3) {
		Tcl_WrongNumArgs(interp, 2, objv, "value");
		return TCL_ERROR;
	}
	
	if (GetGddFromTclObj(interp, objv[2], *rawPV.data) != TCL_OK) {
		return TCL_ERROR;
	}

	/* postEvent, needed for camonitor */
	postUpdateEvent();
	return TCL_OK;
}

void AsynPV::postUpdateEvent() {
    casEventMask select ( server.valueEventMask() | server.logEventMask() );
	rawPV.postEvent(select, *rawPV.data);
	/* postEvent does not wake up the fileDescriptorManager event loop 
	 * Ask the server to activate the wakeup socket */
	server.wakeup();
}


int AsynPV::name(int objc, Tcl_Obj * const objv[]) {
	if (objc != 2) {
		Tcl_WrongNumArgs(interp, 2, objv, "");
		return TCL_ERROR;
	}
	Tcl_SetObjResult(interp, string2Tcl(rawPV.PVname));
	return TCL_OK;
}


int AsynPV::commandfun(int objc, Tcl_Obj * const objv[], Tcl_Obj *& prefix) {
	if (objc != 2 && objc != 3) {
		Tcl_WrongNumArgs(interp, 2, objv, "?prefix?");
		return TCL_ERROR;
	}

	Tcl_MutexLock(&CmdMutex);
	
	/* get callback for asynchronously reading the PV */
	if (objc == 2) {
		if (prefix) {
			Tcl_SetObjResult(interp, prefix);
		}

		Tcl_MutexUnlock(&CmdMutex);
		return TCL_OK;
	}

	/* set callback for asynchronously reading the PV */
	Tcl_Obj *script = objv[2];

	/* remove the old callback script. */
	DecrIfNotNull(prefix);
	prefix = NULL;
	
	/* check if the script is empty, which means delete the callback 
	 * If not, copy the new script */
	int len;
	Tcl_GetStringFromObj(script, &len);
	if (len != 0) {
		Tcl_IncrRefCount(script);
		prefix = script;
	}

	Tcl_MutexUnlock(&CmdMutex);
	return TCL_OK;
}

int AsynPV::readcommand(int objc, Tcl_Obj * const objv[]) {
	return commandfun(objc, objv, readCmdPrefix);
}

int AsynPV::writecommand(int objc, Tcl_Obj * const objv[]) {
	return commandfun(objc, objv, writeCmdPrefix);
}

int AsynPV::lowlimit(int objc, Tcl_Obj * const objv[]) {
	return property(interp, objc, objv, rawPV.lowlimit, "?limit?");
}

int AsynPV::highlimit(int objc, Tcl_Obj * const objv[]) {
	return property(interp, objc, objv, rawPV.highlimit, "?limit?");
}

int AsynPV::precision(int objc, Tcl_Obj * const objv[]) {
	return property(interp, objc, objv, rawPV.precision, "?digits?");
}

int AsynPV::units(int objc, Tcl_Obj * const objv[]) {
	return property(interp, objc, objv, rawPV.units, "?units?");
}

AsynCasPV::AsynCasPV(AsynPV &asynPV, std::string PVname, aitEnum type, unsigned int count) :
	asynPV(asynPV), PVname(PVname), type(type), count(count), 
	lowlimit(0.0), highlimit(10.0), precision(6), units("mm") 
{
	data = new gddScalar(gddAppType_value, type);
	/*data->put(3.14159);*/
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

    gddStatus gdds=value.put(&(*data));
	gddPrintError(gdds);

    return S_cas_success;
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

caStatus AsynCasPV::read ( const casCtx & ctx, gdd & protoIn )
{
	/* check if this needs to be completed asynchronously */
	if (asynPV.readCmdPrefix) {
		Tcl_MutexLock(&CmdMutex);
		/* construct an asynchronous read object */
		AsynCAReadRequest *request = new AsynCAReadRequest(asynPV, ctx);
		/* schedule an event to create the alias in the Tcl thread
		 * and call the associated callback */
		ReadRequestEvent *ev = (ReadRequestEvent *) ckalloc(sizeof(ReadRequestEvent)); 
		// event must be alloc'ed from Tcl
		ev->ev.proc=readRequestInvoke;
		ev->request = request;
		ev->interp = asynPV.interp;
		Tcl_ThreadQueueEvent(asynPV.server.mainid, (Tcl_Event*)ev, TCL_QUEUE_TAIL);
		Tcl_ThreadAlert(asynPV.server.mainid);

		Tcl_MutexUnlock(&CmdMutex);
		return S_casApp_asyncCompletion;	
	} else {
		/* synchronous execution */
		return ft.read ( *this, protoIn );
	}
}

caStatus AsynCasPV::write ( const casCtx &ctx, const gdd & valueIn ) 
{	
	/* check if this needs to be completed asynchronously */
	if (asynPV.writeCmdPrefix) {
		Tcl_MutexLock(&CmdMutex);
		/* construct an asynchronous read object */
		AsynCAWriteRequest *request = new AsynCAWriteRequest(asynPV, ctx, valueIn);
		/* schedule an event to create the alias in the Tcl thread
		 * and call the associated callback */
		WriteRequestEvent *ev = (WriteRequestEvent *) ckalloc(sizeof(WriteRequestEvent)); 
		// event must be alloc'ed from Tcl
		ev->ev.proc=writeRequestInvoke;
		ev->request = request;
		ev->interp = asynPV.interp;
		Tcl_ThreadQueueEvent(asynPV.server.mainid, (Tcl_Event*)ev, TCL_QUEUE_TAIL);
		Tcl_ThreadAlert(asynPV.server.mainid);

		Tcl_MutexUnlock(&CmdMutex);
		return S_casApp_asyncCompletion;	
	} else {
		/* Synchronous operation 
		* Move data to internal storage
		 * Typechecks and conversions are performed in the server library */
		data->put(&valueIn);
		/* a write does not inform other clients. Post event */
		asynPV.postUpdateEvent();
		return S_casApp_success;
	}
}


AsynCAReadRequest::AsynCAReadRequest (AsynPV & pv, const casCtx & ctx) :
	pv (pv), completed(false)
{
	rawRequest = new casAsyncReadIO(ctx);
}


AsynCAReadRequest::~AsynCAReadRequest () {
	if (!completed) {
		/* somehow we were destroyed without a return value
		 * e.g. by calling destroy from Tcl. Signal to the EPICS client
		 * that asynchronous operation has failed */

		rawRequest->postIOCompletion(S_casApp_canceledAsyncIO, *pv.rawPV.data);
		pv.server.wakeup();
	}
}

TCLCLASSIMPLEMENTEXPLICIT(AsynCAReadRequest, return_);

int AsynCAReadRequest::return_ (int objc, Tcl_Obj *const objv[]) {
	int code; 
	if (objc != 3) {
		Tcl_WrongNumArgs(interp, 2, objv, "value");
		code = TCL_ERROR;
		rawRequest->postIOCompletion(S_casApp_canceledAsyncIO, *pv.rawPV.data);
	} else {
		code = GetGddFromTclObj(interp, objv[2], *pv.rawPV.data);
		if (code == TCL_OK) {
			/* signal successful completion and 
			 * return value to EPICS client */
			rawRequest->postIOCompletion(S_cas_success, *pv.rawPV.data);
		} else {
			rawRequest->postIOCompletion(S_casApp_canceledAsyncIO, *pv.rawPV.data);
		}
	}
	completed = true;
	rawRequest = NULL;
	pv.server.wakeup();
	delete this;
	return code;
}

static void bgcallscript(Tcl_Interp *interp, Tcl_Obj *cmd, Tcl_Obj *instName) {
	/* Execute the Tcl callback from the Tcl event loop */

	Tcl_Obj *script = Tcl_DuplicateObj(cmd);
	Tcl_IncrRefCount(script);

	/* append result data and metadata dict */
	int code = Tcl_ListObjAppendElement(interp, script, instName);
	if (code != TCL_OK) {
		goto bgerr;
	}

	Tcl_Preserve(interp);
	code = Tcl_EvalObjEx(interp, script, TCL_EVAL_GLOBAL);

	if (code != TCL_OK) { goto bgerr; }

	Tcl_Release(interp);
	Tcl_DecrRefCount(script);
	/* this event was successfully handled */
	return; 
bgerr:
	/* put error in background */
	Tcl_DecrRefCount(script);

	Tcl_AddErrorInfo(interp, "\n    (epics asynchronous callback script)");
	Tcl_BackgroundException(interp, code);

	/* this event was successfully handled */
	return;
}

int readRequestInvoke(Tcl_Event *p, int flags) {
	/* the event handler run from Tcl */
	ReadRequestEvent *ev = reinterpret_cast<ReadRequestEvent *>(p);
	AsynCAReadRequest *request = ev->request;
	Tcl_Obj *instName = AsynCAReadRequestAliasCreate(ev->interp, request);

	bgcallscript(ev->interp, request->pv.readCmdPrefix, instName);
	return 1;
}

/* Write Request */
AsynCAWriteRequest::AsynCAWriteRequest (AsynPV & pv, const casCtx & ctx, const gdd & gddvalue) :
	pv (pv), completed(false), data (NewTclObjFromGdd(gddvalue))
{
	rawRequest = new casAsyncWriteIO(ctx);
	Tcl_IncrRefCount(data);
}


AsynCAWriteRequest::~AsynCAWriteRequest () {
	if (!completed) {
		/* somehow we were destroyed without a returning cleanly
		 * e.g. by calling destroy from Tcl. Signal to the EPICS client
		 * that asynchronous operation has failed */

		rawRequest->postIOCompletion(S_casApp_canceledAsyncIO);
		pv.server.wakeup();
	}
	Tcl_DecrRefCount(data);
}

int AsynCAWriteRequest::value (int objc, Tcl_Obj *const objv[])
{
	Tcl_SetObjResult(interp, data);
	return TCL_OK;
}


TCLCLASSIMPLEMENTEXPLICIT(AsynCAWriteRequest, value, return_);

int AsynCAWriteRequest::return_ (int objc, Tcl_Obj *const objv[]) {
	int code; 
	if (objc != 2) {
		Tcl_WrongNumArgs(interp, 2, objv, "");
		code = TCL_ERROR;
		rawRequest->postIOCompletion(S_casApp_canceledAsyncIO);
	} else {
		/* signal successful completion  */
		rawRequest->postIOCompletion(S_cas_success);
		code=TCL_OK;
	}
	completed = true;
	rawRequest = NULL;
	pv.server.wakeup();
	delete this;
	return code;
}

int writeRequestInvoke(Tcl_Event *p, int flags) {
	/* the event handler run from Tcl */
	WriteRequestEvent *ev = reinterpret_cast<WriteRequestEvent *>(p);
	AsynCAWriteRequest *request = ev->request;
	Tcl_Obj *instName = AsynCAWriteRequestAliasCreate(ev->interp, request);

	bgcallscript(ev->interp, request->pv.writeCmdPrefix, instName);
	return 1;
}


