/* Macros for interfacing a class with Tcl */
#ifndef TCLCLASS_H
#define TCLCLASS_H
#include "map.h"
#include <tcl.h>

#define TCLCLASSDECLARE(CLASS) \
extern "C" { \
	static int CLASS ## CreateCmd(ClientData cdata, Tcl_Interp *interp, int objc, Tcl_Obj * const objv[]); \
	static int CLASS ## InstanceCmd(ClientData cdata, Tcl_Interp *interp, int objc, Tcl_Obj * const objv[]); \
	static void CLASS ## DeleteCmd(ClientData cdata); \
	static int CLASS ## Link(Tcl_Interp *interp, CLASS *instance); \
}

#define TCLCLASSDECLAREEXPLICIT(CLASS) \
extern "C" { \
	static int CLASS ## InstanceCmd(ClientData cdata, Tcl_Interp *interp, int objc, Tcl_Obj * const objv[]); \
	static void CLASS ## DeleteCmd(ClientData cdata); \
	static int CLASS ## Link(Tcl_Interp *interp, CLASS *instance); \
}

#define INSTANCECMDENTRY(CLASS, FUN) { #FUN, &CLASS::FUN },

#define TCLCLASSSUBCMDS(CLASS, ...) \
	static struct CLASS ## SubCmdEntry { \
		const char *name; \
		int (CLASS::*fun) (int, Tcl_Obj *const[]); \
	} CLASS ## SubCmdTable [] = { \
		MAPARG(INSTANCECMDENTRY, CLASS, __VA_ARGS__) \
		{ "destroy", &CLASS::destroy }, \
		{ NULL, NULL } \
	};

#define TCLCLASSCREATECMD(CLASS) \
	static int CLASS ## CreateCmd(ClientData cdata, Tcl_Interp *interp, int objc, Tcl_Obj * const objv[]) {\
		/* create instance */\
		try {\
			CLASS  *instance = new CLASS(cdata, interp, objc, objv);\
			if (CLASS ## Link(interp, instance) != TCL_OK) { return TCL_ERROR; } \
			return TCL_OK;\
\
		} catch(int code) {\
			return code;\
		}\
	}\

#define TCLCLASSLINK(CLASS) \
	static int CLASS ## Link(Tcl_Interp *interp, CLASS *instance) {\
		static int counter = 0;\
\
\
		instance->interp=interp; \
		/* Create object name */\
		Tcl_Obj* instName=Tcl_ObjPrintf("::AsynCA::" #CLASS "%d", ++counter);\
		Tcl_Command token = Tcl_CreateObjCommand(interp, Tcl_GetString(instName), CLASS ## InstanceCmd, (ClientData) instance, CLASS ## DeleteCmd);\
		instance->ThisCmd = token; \
		Tcl_SetObjResult(interp, instName);\
		return TCL_OK;\
\
	}\


#define TCLCLASSINSTANCECMD(CLASS, ...) \
	TCLCLASSSUBCMDS(CLASS, __VA_ARGS__) \
	static int CLASS ## InstanceCmd(ClientData cdata, Tcl_Interp *interp, int objc, Tcl_Obj * const objv[]) {\
		if (objc < 2) {\
			Tcl_WrongNumArgs(interp, 1, objv, "method");\
			return TCL_ERROR;\
		}\
\
		int index;\
		\
		if (Tcl_GetIndexFromObjStruct(interp, objv[1], \
			CLASS ## SubCmdTable, sizeof(CLASS ## SubCmdEntry), \
			"method", 0, &index) != TCL_OK) {\
			\
			return TCL_ERROR;\
		}	\
\
		CLASS  *instance = static_cast<CLASS *>(cdata);\
		return (instance->*(CLASS ## SubCmdTable[index].fun)) (objc, objv);\
	}\

#define TCLCLASSDELETECMD(CLASS) \
	static void CLASS ## DeleteCmd(ClientData cdata) {\
		CLASS *instance = static_cast<CLASS *>(cdata);\
		if (instance->ThisCmd) { \
			instance->ThisCmd = NULL; \
			delete instance;\
		} \
	}


#define TCLCLASSIMPLEMENT(CLASS, ...) \
	TCLCLASSCREATECMD(CLASS) \
	TCLCLASSLINK(CLASS) \
	TCLCLASSINSTANCECMD(CLASS, __VA_ARGS__) \
	TCLCLASSDELETECMD(CLASS)

#define TCLCLASSIMPLEMENTEXPLICIT(CLASS, ...) \
	TCLCLASSLINK(CLASS) \
	TCLCLASSINSTANCECMD(CLASS, __VA_ARGS__) \
	TCLCLASSDELETECMD(CLASS)


class TclClass {
public:
	Tcl_Interp *interp;
	Tcl_Command ThisCmd;
	TclClass() {
		interp = NULL;
		ThisCmd = NULL;
	}

	virtual ~TclClass () {

		printf("Running Destructor from TclClass\n");
		if (ThisCmd && interp) {
			/* break destructor loop. Signal DeleteCmd that the object is
			 * down already by setting ThisCmd to 0*/
			Tcl_Command cmd = ThisCmd;
			ThisCmd = NULL;
			Tcl_DeleteCommandFromToken(interp, cmd);
			interp = NULL;
		} else {
			printf("Already dead\n");
		}
	}

	int destroy(int objc, Tcl_Obj * const objv[]) {
		delete this;
		return TCL_OK;
	}

	Tcl_Obj * GetCommandFullName() {
		Tcl_Obj *result = Tcl_NewObj();
		Tcl_GetCommandFullName(interp, ThisCmd, result);
		return result;
	}
};

#endif
