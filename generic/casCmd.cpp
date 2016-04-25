/*
 * casCmd.cpp --
 */

#include "casCmd.hpp"


/* Create a new process variable object and return it
 * The callback is invoked for every change of the connection status */

int startServerCmd(ClientData clientData, Tcl_Interp *interp, int objc, Tcl_Obj * const objv[]) {


	int code = AsynServerCreateCmd(clientData, interp, objc, objv);
	AsynCasPV::initFT(); /* initialize dispatch table */
	
	if (code != TCL_OK) { return TCL_ERROR; }

	return TCL_OK;
}

static Tcl_ThreadCreateType EpicsEventLoop (ClientData clientData)
{
	AsynServer * server = static_cast<AsynServer *> (clientData);
	/* Run epics select loop */
	fileDescriptorManager.process(1.0);
	while (server->alive) {
		// printf("Boing \n");
		fileDescriptorManager.process(100.0);
		if (! server->alive) break;
	}
	TCL_THREAD_CREATE_RETURN;
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
		case aitEnumEnum16:
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
	if (bytesread == 1 && msg == 'x') {
		/* terminal signal sent */
		printf("Loop asked to terminate: %c\n", msg);
		/* finishEpicsThread = true; 
		 * done in the server */
	}
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
	
	/* Start thread with the EPICS event loop */
	if (Tcl_CreateThread(&epicsloopid, EpicsEventLoop, static_cast<ClientData>(this),
				TCL_THREAD_STACK_DEFAULT,
				TCL_THREAD_JOINABLE) != TCL_OK) {
		/* Thread did not create correctly */
		Tcl_SetObjResult(interp, Tcl_NewStringObj("Couldn't initialize EPICS event loop", -1));
		throw(TCL_ERROR);
	}
}	

AsynServer::~AsynServer() { 
	printf("AsynServer dying!\n"); 
	/* destroy all PVs from the map */
	alive = false;
	for (auto &p: PVs) {
		delete p.second;
	}
	
	/* wake up the notifier and wait until it is finished */
	alertfd->send('x');
	int resultcode;
	Tcl_JoinThread(epicsloopid, &resultcode);

	delete alertfd;
	printf("AsynServer dead!\n"); 
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


int AsynServer::createPV_(int objc, Tcl_Obj * const objv[]) {
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

int AsynServer::findPV_(int objc, Tcl_Obj * const objv[]) {
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

int AsynServer::listPV_(int objc, Tcl_Obj * const objv[]) {
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
	alertfd->send('w');
}

TCLCLASSIMPLEMENTEXPLICIT(AsynPV, name, read, write, readenum, enumstrings, readcommand, writecommand, lowlimit, highlimit, precision, units);

AsynPV::AsynPV(AsynServer &server, std::string name, aitEnum type, unsigned int count) : 
	server(server), rawPV(*this, name, type, count),
	readCmdPrefix(NULL), writeCmdPrefix(NULL)
{

}

AsynPV::~AsynPV() {
	server.removePV(rawPV.PVname);
}

int AsynPV::read_(int objc, Tcl_Obj * const objv[]) {
	Tcl_SetObjResult(interp, NewTclObjFromGdd(*rawPV.data));
	return TCL_OK;
}

int AsynPV::write_(int objc, Tcl_Obj * const objv[]) {
	if (objc != 3) {
		Tcl_WrongNumArgs(interp, 2, objv, "value");
		return TCL_ERROR;
	}
	
	Tcl_Obj *value = objv[2];

	if (rawPV.putTclObj(interp, value) != TCL_OK) {
			return TCL_ERROR;
	}
	
	/* postEvent, needed for camonitor */
	postUpdateEvent();
	return TCL_OK;
}

int AsynPV::readenum_(int objc, Tcl_Obj * const objv[]) {
	if (objc != 2) {
		Tcl_WrongNumArgs(interp, 2, objv, "");
		return TCL_ERROR;
	}
	
	if (rawPV.type != aitEnumEnum16) {
		Tcl_SetObjResult(interp, Tcl_NewStringObj("Process variable is not an enum", -1));
		return TCL_ERROR;
	}

	aitEnum16 ind;
	rawPV.data->get(ind);

	if (ind < rawPV.enumstrings.size()) {
		Tcl_SetObjResult(interp, NewTclObj(rawPV.enumstrings[ind]));
		return TCL_OK;
	}
	/* index too large - return as an int */

	Tcl_SetObjResult(interp, NewTclObj(ind));
	return TCL_OK;

}

int AsynPV::enumstrings_(int objc, Tcl_Obj * const objv[]) {
	if (objc != 2 && objc != 3) {
		Tcl_WrongNumArgs(interp, 2, objv, "?values?");
		return TCL_ERROR;
	}

	if (rawPV.type != aitEnumEnum16) {
		Tcl_SetObjResult(interp, Tcl_NewStringObj("Process variable is not an enum", -1));
		return TCL_ERROR;
	}
	
	/* return the list of states */
	if (objc == 2) {
		Tcl_Obj *result = Tcl_NewObj();
		for (const auto &str : rawPV.enumstrings) {
			Tcl_ListObjAppendElement(NULL, result, NewTclObj(str));
		}
		Tcl_SetObjResult(interp, result);
		return TCL_OK;
	}

	/* set the list of states */
	int N; Tcl_Obj **el;
	if (Tcl_ListObjGetElements(interp, objv[2], &N, &el) != TCL_OK) {
		/* List couldn't be parsed */
		return TCL_ERROR;
	}

	if (N >  MAX_ENUM_STATES) {
		Tcl_SetObjResult(interp, Tcl_ObjPrintf("Too many enum states (%d), only %d allowed", N, MAX_ENUM_STATES));
		return TCL_ERROR;
	}

	std::vector<std::string> newstrings;
	std::unordered_map<std::string, aitEnum16> newmap;
	
	for (int i=0; i<N; i++) {
		int length;
		char *bytes = Tcl_GetStringFromObj(el[i], &length);
		if (length >  MAX_ENUM_STRING_SIZE) {
			Tcl_SetObjResult(interp, 
				Tcl_ObjPrintf("Enum string >%s< too long (%d chars), only %d allowed", 
				bytes, length, MAX_ENUM_STRING_SIZE));
			
			return TCL_ERROR;
		}

		std::string key = std::string(bytes, length);
		newstrings.push_back(key);
		/* check if this key is already there */
		if (newmap.find(key) != newmap.end()) {
			Tcl_SetObjResult(interp, Tcl_ObjPrintf("Duplicate keys >%s< in enum state list at position %d and %d",
				key.c_str(), newmap[key], i));
			return TCL_ERROR;
		}

		newmap[key] = i;
	}

	rawPV.enumstrings = newstrings;
	rawPV.enummap = newmap;

	return TCL_OK;

}

void AsynPV::postUpdateEvent() {
    casEventMask select ( server.valueEventMask() | server.logEventMask() );
	rawPV.postEvent(select, *rawPV.data);
	/* postEvent does not wake up the fileDescriptorManager event loop 
	 * Ask the server to activate the wakeup socket */
	server.wakeup();
}


int AsynPV::name_(int objc, Tcl_Obj * const objv[]) {
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

int AsynPV::readcommand_(int objc, Tcl_Obj * const objv[]) {
	return commandfun(objc, objv, readCmdPrefix);
}

int AsynPV::writecommand_(int objc, Tcl_Obj * const objv[]) {
	return commandfun(objc, objv, writeCmdPrefix);
}

int AsynPV::lowlimit_(int objc, Tcl_Obj * const objv[]) {
	return property(interp, objc, objv, rawPV.lowlimit, "?limit?");
}

int AsynPV::highlimit_(int objc, Tcl_Obj * const objv[]) {
	return property(interp, objc, objv, rawPV.highlimit, "?limit?");
}

int AsynPV::precision_(int objc, Tcl_Obj * const objv[]) {
	return property(interp, objc, objv, rawPV.precision, "?digits?");
}

int AsynPV::units_(int objc, Tcl_Obj * const objv[]) {
	return property(interp, objc, objv, rawPV.units, "?units?");
}

AsynCasPV::AsynCasPV(AsynPV &asynPV, std::string PVname, aitEnum type, unsigned int count) :
	asynPV(asynPV), PVname(PVname), type(type), count(count), 
	lowlimit(0.0), highlimit(10.0), precision(6), units("mm"), enumstrings(0)
{
	data = new gddScalar(gddAppType_value, type);
	/*data->put(3.14159);*/
	if (type == aitEnumEnum16) {
		/* fake enum states */
		enumstrings.push_back("On");
		enumstrings.push_back("Off");
		enumstrings.push_back("Tri-state");
	}
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


class aitFixedStringDestructor: public gddDestructor {
    virtual void run (void *);
};

void aitFixedStringDestructor::run ( void * pUntyped )
{
    aitFixedString *ps = (aitFixedString *) pUntyped;
    delete [] ps;
}

// returns the eneumerated state strings
// for a discrete channel
caStatus AsynCasPV::getEnums ( gdd & enumsIn )
{
	if ( type != aitEnumEnum16 || enumstrings.size() == 0 ) {
		/* for non-enums, it makes no sense 
		 * retur success also if no strings are defined */
		return S_cas_success;
	}

	int N = enumstrings.size();
	
	aitFixedString *str;
	aitFixedStringDestructor *des;

	str = new aitFixedString[N];
	des = new aitFixedStringDestructor;

	for (int i=0; i<N; i++) {
		strncpy (str[i].fixed_string, enumstrings[i].c_str(), sizeof(str[i].fixed_string));
	}
	
	enumsIn.setDimension(1);
	enumsIn.setBound (0,0,N);
	enumsIn.putRef (str, des);
	
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
		AsynCAReadRequest *request = new AsynCAReadRequest(asynPV, ctx, protoIn);
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

int AsynCasPV::putTclObj(Tcl_Interp *interp, Tcl_Obj *value) {
	/* Try to convert the Tcl_Obj into the data type of the gdd.
	 * Return a Tcl success code */
	
	if (data->dimension() != 0) {
		if (interp)
			Tcl_SetObjResult(interp, Tcl_ObjPrintf("Unimplemented vector put for %d items, must be 1", data->dimension()));
		return TCL_ERROR;
	}

	switch (data->primitiveType()) {
		case aitEnumFloat64:
		case aitEnumFloat32: {
			double d;
			if (Tcl_GetDoubleFromObj(interp, value, &d) != TCL_OK) {
				return TCL_ERROR;
			}
			/* finally put it and update the time stamp */
			data->putConvert(d);
			
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
								if (interp) \
									Tcl_SetObjResult(interp, Tcl_NewStringObj("Value outside range for type " STRINGIFY(AITYPE), -1));\
								 return TCL_ERROR;\
							 }\
							 data->put(tval); \
							 break; \
						 }

		INTVALCONV(aitEnumUint8, aitUint8)
		INTVALCONV(aitEnumInt8, aitInt8)
		INTVALCONV(aitEnumUint16, aitUint16)
		INTVALCONV(aitEnumInt16, aitInt16)
		INTVALCONV(aitEnumUint32, aitUint32)
		INTVALCONV(aitEnumInt32, aitInt32)
		
#undef INTVALCONV
		case aitEnumFixedString: {
			int len;
			const char *bytes = Tcl_GetStringFromObj(value, &len);
			const int maxlen = sizeof(aitFixedString);
			if (len > maxlen) {
				if (interp)
					Tcl_SetObjResult(interp, Tcl_ObjPrintf("String too long (%d bytes), max %d bytes allowed", len, maxlen));
				return TCL_ERROR;
			}
			
			
			data->putConvert(bytes);
			break;
		}

		case aitEnumEnum16: {
		
			auto it = enummap.find(Tcl_GetString(value));
			/* fails with embedded NULL character, but this is not allowed either */
			if (it != enummap.end()) {
				data->putConvert(it->second);
			} else {
				int index;
				if (Tcl_GetIntFromObj(interp, value, &index) != TCL_OK) {
					if (interp)
						Tcl_SetObjResult(interp, Tcl_ObjPrintf(">%s< is not a valid enum state", Tcl_GetString(value)));
					return TCL_ERROR;
				}
				if (index < 0 || index > MAX_ENUM_STATES) {
					if (interp)
						Tcl_SetObjResult(interp, Tcl_ObjPrintf("Enum state %d out of range", index));
					return TCL_ERROR;
				}

				data->putConvert(index);
			}

			break;
		}

		default: {
			if (interp)
				Tcl_SetObjResult(interp, Tcl_ObjPrintf("Unimplemented data type %d", data->primitiveType()));
			return TCL_ERROR;
		}
	}

	aitTimeStamp gddts(epicsTime::getCurrent());
	data->setTimeStamp(&gddts);
	return TCL_OK;
}

AsynCARawReadRequest::AsynCARawReadRequest(AsynCAReadRequest &boxedrequest, const casCtx & ctx) :
	casAsyncReadIO(ctx), boxedrequest(boxedrequest), completed(false) 
{	}


AsynCARawReadRequest::~AsynCARawReadRequest() {
	if (!completed) {
		/* Huhu. The client has killed the connection. We need to signal the corresponding Tcl object
		 * that it's gone */
		boxedrequest.droppedrequest();
	}
}


AsynCAReadRequest::AsynCAReadRequest (AsynPV & pv, const casCtx & ctx, gdd & protoIn) :
	pv (pv), completed(false), proto(protoIn)
{
	rawRequest = new AsynCARawReadRequest(*this, ctx);
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

TCLCLASSIMPLEMENTEXPLICIT(AsynCAReadRequest, return);

int AsynCAReadRequest::return_ (int objc, Tcl_Obj *const objv[]) {
	int code;
	if (!completed) {
		if (objc != 3) {
			Tcl_WrongNumArgs(interp, 2, objv, "value");
			code = TCL_ERROR;
			rawRequest->postIOCompletion(S_casApp_canceledAsyncIO, *pv.rawPV.data);
		} else {
			code = pv.rawPV.putTclObj(interp, objv[2]);
			if (code == TCL_OK) {
				/* signal successful completion and 
				 * return value to EPICS client */
				pv.rawPV.ft.read(pv.rawPV, *proto);
				rawRequest->postIOCompletion(S_cas_success, *proto);
			} else {
				rawRequest->postIOCompletion(S_casApp_canceledAsyncIO, *pv.rawPV.data);
			}
		}
		rawRequest->completed=true;
		completed = true;
		rawRequest = NULL;
		pv.server.wakeup();
	} else {
		/* return was called, despite the original request not existent. 
		 * Probably the client dropped the connection */
		Tcl_SetObjResult(interp, Tcl_NewStringObj("Request cancelled by client", -1));
		code = TCL_OK;
	}
	delete this;
	return code;
}

void AsynCAReadRequest::droppedrequest() {
	completed = true;
	rawRequest = NULL;
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
AsynCARawWriteRequest::AsynCARawWriteRequest(AsynCAWriteRequest &boxedrequest, const casCtx & ctx) :
	casAsyncWriteIO(ctx), boxedrequest(boxedrequest), completed(false) 
{	}


AsynCARawWriteRequest::~AsynCARawWriteRequest() {
	if (!completed) {
		/* Huhu. The client has killed the connection. We need to signal the corresponding Tcl object
		 * that it's gone */
		boxedrequest.droppedrequest();
	}
}


TCLCLASSIMPLEMENTEXPLICIT(AsynCAWriteRequest, value, return);

AsynCAWriteRequest::AsynCAWriteRequest (AsynPV & pv, const casCtx & ctx, const gdd & gddvalue) :
	pv (pv), completed(false), data (NewTclObjFromGdd(gddvalue))
{
	rawRequest = new AsynCARawWriteRequest(*this, ctx);
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

int AsynCAWriteRequest::value_ (int objc, Tcl_Obj *const objv[])
{
	Tcl_SetObjResult(interp, data);
	return TCL_OK;
}

int AsynCAWriteRequest::return_ (int objc, Tcl_Obj *const objv[]) {
	int code;
	if (!completed) {
		if (objc != 2) {
			Tcl_WrongNumArgs(interp, 2, objv, "");
			code = TCL_ERROR;
			rawRequest->postIOCompletion(S_casApp_canceledAsyncIO);
		} else {
			/* signal successful completion  */
			rawRequest->postIOCompletion(S_cas_success);
			code=TCL_OK;
		}
		rawRequest -> completed = true;
		completed = true;
		rawRequest = NULL;
		pv.server.wakeup();
	} else {
		/* return was called, despite the original request not existent. 
		 * Probably the client dropped the connection */
		Tcl_SetObjResult(interp, Tcl_NewStringObj("Request cancelled by client", -1));
		code = TCL_OK;
	}
	delete this;
	return code;
}

void AsynCAWriteRequest::droppedrequest() {
	completed = true;
	rawRequest = NULL;
}

int writeRequestInvoke(Tcl_Event *p, int flags) {
	/* the event handler run from Tcl */
	WriteRequestEvent *ev = reinterpret_cast<WriteRequestEvent *>(p);
	AsynCAWriteRequest *request = ev->request;
	Tcl_Obj *instName = AsynCAWriteRequestAliasCreate(ev->interp, request);

	bgcallscript(ev->interp, request->pv.writeCmdPrefix, instName);
	return 1;
}


