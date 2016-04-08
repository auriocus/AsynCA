#ifndef CASEXPORT_H
#define CASEXPORT_H

#ifdef __cplusplus
extern "C" {
#endif

int startServerCmd(ClientData clientData, Tcl_Interp *interp, int objc, Tcl_Obj * const objv[]); 
int createPVCmd(ClientData clientData, Tcl_Interp *interp, int objc, Tcl_Obj * const objv[]); 

#ifdef __cplusplus
}
#endif 

#endif
