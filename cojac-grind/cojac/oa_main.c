
/*--------------------------------------------------------------------*/
/*--- Cojac-grind numerical problem sniffer.              oa_main  ---*/
/*--------------------------------------------------------------------*/

/*
   This file is part of Cojac-grind, which watches arithmetic operations to
   detect overflows, cancellation, smearing, and other suspicious phenomena.

   Copyright (C) 2011-2014 Frederic Bapst & Luis Domingues
      frederic.bapst@gmail.com, domigues.luis@gmail.com

   This program is free software; you can redistribute it and/or
   modify it under the terms of the GNU General Public License as
   published by the Free Software Foundation; either version 2 of the
   License, or (at your option) any later version.

   This program is distributed in the hope that it will be useful, but
   WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
   General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program; if not, write to the Free Software
   Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA
   02111-1307, USA.

   The GNU General Public License is contained in the file COPYING.
 */

#include "pub_tool_basics.h"
#include "pub_tool_tooliface.h"
#include "pub_tool_libcassert.h"
#include "pub_tool_libcprint.h"
#include "pub_tool_debuginfo.h"
#include "pub_tool_libcbase.h"
#include "pub_tool_options.h"
#include "pub_tool_options.h"
#include "pub_tool_mallocfree.h"
#include "pub_tool_machine.h"     // VG_(fnptr_to_fnentry)
#include "pub_tool_threadstate.h"
#include "libvex_guest_amd64.h"
#include "oa_include.h"
#include "limits.h"
/*--------------------------------------------------------------------*/
#define OA_IOP_MAX 1000    //Iop_Rsqrte32x4-Iop_INVALID = ~752
#define OA_CALL_MAX 10
#define FP_XMM0_REG 224   //Is the same for F64 or F32

static Iop_Cojac_attributes oa_all_iop_attr[OA_IOP_MAX];
static Call_Cojac_attributes oa_all_call_attr[OA_CALL_MAX];
static IRType thisWordWidth;
cojacOptions OA_(options);
/*--------------------------------------------------------------------*/
Iop_Cojac_attributes* OA_(get_Iop_struct)(IROp op) {
  if (op-Iop_INVALID >= OA_IOP_MAX)
    VG_(tool_panic)("Too many IROps nowadays...");
  return &(oa_all_iop_attr[op-Iop_INVALID]);
}

Call_Cojac_attributes* OA_(get_Call_struct)(OA_Call call) {
  if (call-Call_INVALID >= OA_CALL_MAX)
    VG_(tool_panic)("Too many Calls...");
  return &(oa_all_call_attr[call-Call_INVALID]);
}

static void init_iop(IROp op, const char* name, void* callI32, void* callI64) {
  oa_all_iop_attr[op-Iop_INVALID].op=op;
  oa_all_iop_attr[op-Iop_INVALID].name=name;
  oa_all_iop_attr[op-Iop_INVALID].callbackI32=callI32;
  oa_all_iop_attr[op-Iop_INVALID].callbackI64=callI64;
}

static void init_call(OA_Call call, const char* name, void* callI32, void* callI64, OA_Param_Type pType) {
  oa_all_call_attr[call-Call_INVALID].call=call;
  oa_all_call_attr[call-Call_INVALID].name=name;
  oa_all_call_attr[call-Call_INVALID].callbackI32=callI32;
  oa_all_call_attr[call-Call_INVALID].callbackI64=callI64;
  oa_all_call_attr[call-Call_INVALID].paramType=pType;
}


static void populate_iop_struct(void) {
  Iop_Cojac_attributes a;
  a.callbackI32=NULL; a.callbackI64=NULL; a.name=""; a.occurrences=0;
  int i=0;
  for(i=0; i<OA_IOP_MAX; i++)
    oa_all_iop_attr[i]=a;

  if (OA_(options).i32) {
    init_iop(Iop_Add32,   "Add32",  oa_callbackI32_2x32, oa_callbackI64_2x32);
    init_iop(Iop_Sub32,   "Sub32",  oa_callbackI32_2x32, oa_callbackI64_2x32);
    init_iop(Iop_Mul32,   "Mul32",  oa_callbackI32_2x32, oa_callbackI64_2x32);
    init_iop(Iop_DivS32,  "DivS32", oa_callbackI32_2x32, oa_callbackI64_2x32);
  }

  if (OA_(options).i64) {
    init_iop(Iop_Add64,   "Add64",  oa_callbackI32_2x64, oa_callbackI64_2x64);
    init_iop(Iop_Sub64,   "Sub64",  oa_callbackI32_2x64, oa_callbackI64_2x64);
    init_iop(Iop_Mul64,   "Mul64",  oa_callbackI32_2x64, oa_callbackI64_2x64);
    init_iop(Iop_DivS64,  "DivS64", oa_callbackI32_2x64, oa_callbackI64_2x64);
  }


  if (OA_(options).i16) {
    init_iop(Iop_Add16,   "Add16",  oa_callbackI32_2x16, oa_callbackI64_2x16);
    init_iop(Iop_Sub16,   "Sub16",  oa_callbackI32_2x16, oa_callbackI64_2x16);
    init_iop(Iop_Mul16,   "Mul16",  oa_callbackI32_2x16, oa_callbackI64_2x16);
  }

  if (OA_(options).castToI16) {
    init_iop(Iop_32to16,  "32to16", oa_callbackI32_1x32, oa_callbackI64_1x32);
  }
  
  if (OA_(options).castFromF64) {
    init_iop(Iop_F64toI32S, "F64toI32S",  oa_callbackI64_1xI32_1xF64, oa_callbackI64_1xI32_1xF64);
    init_iop(Iop_F64toI64S, "F64toI64S",  oa_callbackI64_1xI32_1xF64, oa_callbackI64_1xI32_1xF64);
    init_iop(Iop_F64toF32, "F64toF32",  oa_callbackI64_1xI32_1xF64, oa_callbackI64_1xI32_1xF64);
  }

  if (OA_(options).f32) {
    init_iop(Iop_AddF32,  "AddF32", oa_callbackI32_2xF32, oa_callbackI64_2xF32);
    init_iop(Iop_SubF32,  "SubF32", oa_callbackI32_2xF32, oa_callbackI64_2xF32);
    init_iop(Iop_MulF32,  "MulF32", oa_callbackI32_2xF32, oa_callbackI64_2xF32);
    init_iop(Iop_DivF32,  "DivF32", oa_callbackI32_2xF32, oa_callbackI64_2xF32);
    init_iop(Iop_Add32F0x4,  "Add32F0x4", oa_callbackI32_2xF32, oa_callbackI64_2xF32);
    init_iop(Iop_Sub32F0x4,  "Sub32F0x4", oa_callbackI32_2xF32, oa_callbackI64_2xF32);
    init_iop(Iop_Mul32F0x4,  "Mul32F0x4", oa_callbackI32_2xF32, oa_callbackI64_2xF32);
    init_iop(Iop_Div32F0x4,  "Div32F0x4", oa_callbackI32_2xF32, oa_callbackI64_2xF32);
  }

  if (OA_(options).f64) {
    init_iop(Iop_AddF64,  "AddF64",     oa_callbackI32_2xF64, oa_callbackI64_2xF64);
    init_iop(Iop_SubF64,  "SubF64",     oa_callbackI32_2xF64, oa_callbackI64_2xF64);
    init_iop(Iop_MulF64,  "MulF64",     oa_callbackI32_2xF64, oa_callbackI64_2xF64);
    init_iop(Iop_DivF64,  "DivF64",     oa_callbackI32_2xF64, oa_callbackI64_2xF64);
    init_iop(Iop_Add64F0x2,"Add64F0x2", oa_callbackI32_2xF64, oa_callbackI64_2xF64);
    init_iop(Iop_Add64Fx2, "Add64Fx2",  oa_callbackI32_2xF64, oa_callbackI64_2xF64);
    init_iop(Iop_Sub64F0x2,"Sub64F0x2", oa_callbackI32_2xF64, oa_callbackI64_2xF64);
    init_iop(Iop_Sub64Fx2, "Sub64Fx2",  oa_callbackI32_2xF64, oa_callbackI64_2xF64);
    init_iop(Iop_Mul64F0x2,"Mul64F0x2", oa_callbackI32_2xF64, oa_callbackI64_2xF64);
    init_iop(Iop_Mul64Fx2, "Mul64Fx2",  oa_callbackI32_2xF64, oa_callbackI64_2xF64);
    init_iop(Iop_Div64F0x2,"Div64F0x2", oa_callbackI32_2xF64, oa_callbackI64_2xF64);
    init_iop(Iop_Div64Fx2, "Div64Fx2",  oa_callbackI32_2xF64, oa_callbackI64_2xF64);
    init_iop(Iop_CmpF64, "CmpF64",  oa_callbackI32_2xF64, oa_callbackI64_2xF64);
  }
  
  if (OA_(options).mathOp) {
    init_iop(Iop_Sqrt64Fx2, "Sqrt64Fx2",  oa_callbackI32_1xF64, oa_callbackI64_1xF64);
  }
}

/*On init call, the name og the function need to be the same as the real function name.
The matching is done on it. See MAX_NAME_LENGTH define for max length function name*/
static void populate_call_struct(void) {
  Call_Cojac_attributes a;
  a.callbackI32=NULL; a.callbackI64=NULL; a.name=""; a.occurrences=0;
  int i=0;
  for(i=0; i<OA_CALL_MAX; i++)
    oa_all_call_attr[i]=a;
  
  if (OA_(options).mathOp) {
    //1xF64 param
    init_call(Call_Asin, "asin",  oa_callbackI32_call_1xF64, oa_callbackI64_call_1xF64, Call_1xF64);
    init_call(Call_Sqrt, "sqrt",  oa_callbackI32_call_1xF64, oa_callbackI64_call_1xF64, Call_1xF64);
    init_call(Call_Log, "log",  oa_callbackI32_call_1xF64, oa_callbackI64_call_1xF64, Call_1xF64);
    
    //1xF32 param
    init_call(Call_Asinf, "asinf",  oa_callbackI32_call_1xF32, oa_callbackI64_call_1xF32, Call_1xF32);
    init_call(Call_Sqrtf, "sqrtf",  oa_callbackI32_call_1xF32, oa_callbackI64_call_1xF32, Call_1xF32);
    init_call(Call_Logf, "logf",  oa_callbackI32_call_1xF32, oa_callbackI64_call_1xF32, Call_1xF32);
  }
}

static Bool dropV128HiPart(IROp op) {
	switch(op) {
		case Iop_Add64F0x2:
		case Iop_Sub64F0x2:
		case Iop_Mul64F0x2:
		case Iop_Div64F0x2:
			return True;
		default: return False;
	}
}

/*--------------------------------------------------------------------*/
static void get_debug_info(Addr instr_addr, HChar file[COJAC_FILE_LEN],
                           HChar fn[COJAC_FCT_LEN], UInt* line, Bool* isLocated) {
  HChar dir[COJAC_FILE_LEN];
  Bool found_dirname;
  Bool found_file_line = VG_(get_filename_linenum)(
      instr_addr,
      file, COJAC_FILE_LEN,
      dir,  COJAC_FILE_LEN, &found_dirname,
      line
  );
  Bool found_fn = VG_(get_fnname)(instr_addr, fn, COJAC_FCT_LEN);
  *isLocated=True;
  if (!found_file_line) {
    VG_(strcpy)(file, "???");
    *line = 0;
    *isLocated=False;
  }
  if (!found_fn) {
    VG_(strcpy)(fn,  "???");
    *isLocated=False;
  }
  if (found_dirname) {
    // +1 for the '/'.
    tl_assert(VG_(strlen)(dir) + VG_(strlen)(file) + 1 < COJAC_FILE_LEN);
    VG_(strcat)(dir, "/");     // Append '/'
    VG_(strcat)(dir, file);    // Append file to dir
    VG_(strcpy)(file, dir);    // Move dir+file to file
  }
}
//-----------------------------------------------------------------
static void* callbackFromIROp(IROp op) {
  if (thisWordWidth==Ity_I32) {
    return OA_(get_Iop_struct)(op)->callbackI32;
  } else if (thisWordWidth==Ity_I64) {
    return OA_(get_Iop_struct)(op)->callbackI64;
  } else {
    VG_(tool_panic)("unsupported architecture...");
    return OA_(get_Iop_struct)(op)->callbackI32;
  }
}

//-----------------------------------------------------------------
static void* callbackFromOACall(OA_Call call){
  if (thisWordWidth==Ity_I32) {
    return OA_(get_Call_struct)(call)->callbackI32;
  } else if (thisWordWidth==Ity_I64) {
    return OA_(get_Call_struct)(call)->callbackI64;
  } else {
    VG_(tool_panic)("unsupported architecture...");
    return OA_(get_Call_struct)(call)->callbackI32;
  }
}
//-----------------------------------------------------------------
static const char * strFromIROp(IROp op) {
  return OA_(get_Iop_struct)(op)->name;
}

//-----------------------------------------------------------------
static void updateStats(IROp op) {
  OA_(get_Iop_struct)(op)->occurrences++;
}

//-----------------------------------------------------------------
static const char * strFromOACall(OA_Call call) {
  return OA_(get_Call_struct)(call)->name;
}

//-----------------------------------------------------------------
static void updateStatsCall(OA_Call call) {
  OA_(get_Call_struct)(call)->occurrences++;
}
//-----------------------------------------------------------------
static void print_instrumentation_stats(void) {
  VG_(message)(Vg_UserMsg, "Cojac instrumentation statistics:\n");
  Int i=0;
  for(i=0; i<OA_IOP_MAX; i++) {
    Iop_Cojac_attributes a=oa_all_iop_attr[i];
    if (a.occurrences >0)
      VG_(message)(Vg_UserMsg, "%s \t %lld \n", a.name, a.occurrences);
  }
  for(i=0; i<OA_CALL_MAX; i++){
    Call_Cojac_attributes a=oa_all_call_attr[i];
    if (a.occurrences >0)
      VG_(message)(Vg_UserMsg, "%s \t %lld \n", a.name, a.occurrences);
  }
}


//-----------------------------------------------------------------
/* Converts an IRExpr into 1 or 2 "flat" expressions of type Int */
static void packToI32 ( IRSB* sb, IRExpr* e, IRExpr* res[2]) {
  //IRType eType = Ity_I32;
  IRTemp tmp64;
  res[1]=NULL;
  switch (typeOfIRExpr(sb->tyenv,e)) {
    case Ity_I1:   res[0]= IRExpr_Unop(Iop_1Uto32,e);  break;
    case Ity_I8:   res[0]= IRExpr_Unop(Iop_8Uto32,e);  break;
    case Ity_I16:  res[0]= IRExpr_Unop(Iop_16Uto32,e); break;
    case Ity_I32:  res[0]= e; return;
    case Ity_F32:  res[0]= IRExpr_Unop(Iop_ReinterpF32asI32,e); break;
    case Ity_I64:
      res[0]= IRExpr_Unop(Iop_64to32, e);
      res[1]= IRExpr_Unop(Iop_64HIto32, e);
      break;
    case Ity_F64:
      tmp64 = newIRTemp(sb->tyenv, Ity_I64);
      addStmtToIRSB(sb, IRStmt_WrTmp(tmp64, IRExpr_Unop(Iop_ReinterpF64asI64,e)));
      res[0]= IRExpr_Unop(Iop_64to32,   IRExpr_RdTmp(tmp64) );
      res[1]= IRExpr_Unop(Iop_64HIto32, IRExpr_RdTmp(tmp64) );
      break;
    case Ity_V128:   /* 128-bit SIMD */
    case Ity_I128:   /* 128-bit scalar */
    case Ity_INVALID:
    default: VG_(tool_panic)("COJAC cannot packToI32..."); break;
  }
  IRTemp myArg = newIRTemp(sb->tyenv, Ity_I32);
  addStmtToIRSB(sb, IRStmt_WrTmp(myArg, res[0]));
  res[0]=IRExpr_RdTmp(myArg);    // now that's a "flat" expression
  if (res[1]==NULL) return;
  IRTemp myArg2 = newIRTemp(sb->tyenv, Ity_I32);
  addStmtToIRSB(sb, IRStmt_WrTmp(myArg2, res[1]));
  res[1]=IRExpr_RdTmp(myArg2);   // now that's a "flat" expression
}


//-----------------------------------------------------------------
/* Converts an IRExpr into 1 or 2 "flat" expressions of type Long */
static void packToI64 ( IRSB* sb, IRExpr* e, IRExpr* res[2], IROp irop) {
  res[1]=NULL;  // won't be used at all
  IRTemp tmp64;
  switch (typeOfIRExpr(sb->tyenv,e)) {
    case Ity_I1:   res[0]= IRExpr_Unop(Iop_1Uto64,e);  break;
    case Ity_I8:   res[0]= IRExpr_Unop(Iop_8Uto64,e);  break;
    case Ity_I16:  res[0]= IRExpr_Unop(Iop_16Uto64,e); break;
    case Ity_I32:  res[0]= IRExpr_Unop(Iop_32Uto64,e); break;
    case Ity_F32:  e = IRExpr_Unop(Iop_ReinterpF32asI32,e);
                   IRTemp aux= newIRTemp(sb->tyenv, Ity_I32);
                   addStmtToIRSB(sb, IRStmt_WrTmp(aux, e));
                   e=IRExpr_RdTmp(aux);
                   res[0]= IRExpr_Unop(Iop_32Uto64,e);  break;
    case Ity_I64:  res[0]= e; return;
    case Ity_F64:  res[0]= IRExpr_Unop(Iop_ReinterpF64asI64,e); break;
    case Ity_V128:   /* 128-bit SIMD */
        //tmp64 = newIRTemp(sb->tyenv, Ity_I64);
        //addStmtToIRSB(sb, IRStmt_WrTmp(tmp64, IRExpr_Unop(Iop_V128to64,e)));
        //res[0]= IRExpr_Unop(Iop_64to32,   IRExpr_RdTmp(tmp64) );
        //res[1]= IRExpr_Unop(Iop_64HIto32, IRExpr_RdTmp(tmp64) );
    	res[0]= IRExpr_Unop(Iop_V128to64,   e );
    	if (dropV128HiPart(irop)) break;
    	res[1]= IRExpr_Unop(Iop_V128HIto64, e );
    	break;

    case Ity_I128:   /* 128-bit scalar */
    case Ity_INVALID:
    default: VG_(tool_panic)("COJAC cannot packToI64..."); break;
  }
  IRTemp myArg = newIRTemp(sb->tyenv, Ity_I64);
  addStmtToIRSB(sb, IRStmt_WrTmp(myArg, res[0]));
  res[0]=IRExpr_RdTmp(myArg);  // now that's a "flat" expression
  if (res[1]==NULL) return;  // ... else it was 128-bit SIMD
  IRTemp myArg2 = newIRTemp(sb->tyenv, Ity_I64);
  addStmtToIRSB(sb, IRStmt_WrTmp(myArg2, res[1]));
  res[1]=IRExpr_RdTmp(myArg2);   // now that's a "flat" expression
}
//-----------------------------------------------------------------
/* Converts an IRExpr into 1 or 2 "flat" expressions of type Long */
static void packToI32orI64 ( IRSB* sb, IRExpr* e, IRExpr* res[2], IROp irop) {
  if (thisWordWidth==Ity_I32){
    packToI32(sb, e, res);
  } else if (thisWordWidth==Ity_I64) {
    packToI64(sb,e,res, irop);
  } else {
    VG_(tool_panic)("unsupported architecture...");
  }
}

//-----------------------------------------------------------------
// Determines when it is worth inspecting arithmetic. Typical "modes":
// - always (return False)
// - when we know the functionName+sourcefile:line (return !ic->isLocated)
// - in a particular source file (?)
// - ...
static Bool not_worth_watching(OA_InstrumentContext ic) {
  if (OA_(options).isAggr) return False;
  return !ic->isLocated;
}
//-----------------------------------------------------------------
static OA_InstrumentContext contextForIop(Addr64 cia, IROp op) {
  HChar thisFct[]="contextForIop";
  OA_InstrumentContext ic=VG_(malloc)(thisFct, sizeof(OA_InstrumentContext_));
  UInt     line;
  HChar    filename[COJAC_FILE_LEN];
  HChar    fctname[COJAC_FCT_LEN];
  get_debug_info((Addr)cia, filename, fctname, &line, &(ic->isLocated));
  ic->string = strFromIROp(op);
  ic->addr = (Addr)cia;
  ic->op=op;
  ic->type = IsIROp;
  //VG_(sprintf)(ic->string, "%s %s(), %s:%d", strFromIROp(op), fctname, filename, line);
  //VG_(sprintf)(ic->string, "%s", strFromIROp(op));
  return ic;
}
//-----------------------------------------------------------------
static OA_InstrumentContext contextForCall(Addr64 cia, OA_Call call) {
  HChar thisFct[]="contextForCall";
  OA_InstrumentContext ic=VG_(malloc)(thisFct, sizeof(OA_InstrumentContext_));
  UInt     line;
  HChar    filename[COJAC_FILE_LEN];
  HChar    fctname[COJAC_FCT_LEN];
  get_debug_info((Addr)cia, filename, fctname, &line, &(ic->isLocated));
  ic->string = strFromOACall(call);
  ic->addr = (Addr)cia;
  ic->call=call;
  ic->type = IsCall;
  //VG_(sprintf)(ic->string, "%s %s(), %s:%d", strFromIROp(op), fctname, filename, line);
  //VG_(sprintf)(ic->string, "%s", strFromOACall(call));
  return ic;
}

//-----------------------------------------------------------------
static void instrument_Unop(IRSB* sb, IRStmt* st, Addr64 cia) {
  HChar thisFct[]="instrument_Unop";
  IRExpr *op = st->Ist.WrTmp.data;
  IROp irop=op->Iex.Unop.op;
  void* f=callbackFromIROp(irop);
  if (f == NULL) return;
  OA_InstrumentContext inscon=contextForIop(cia, irop);
  if (not_worth_watching(inscon))
    return;  // filter events that can't be attached to source-code location
  updateStats(inscon->op);
  IRExpr* oa_event_expr = mkIRExpr_HWord( (HWord)inscon );
  IRExpr * args[2];
  packToI32orI64(sb, op->Iex.Unop.arg, args, irop);
  IRExpr** argv = mkIRExprVec_2(args[0], oa_event_expr);
  IRDirty* di = unsafeIRDirty_0_N( 2, thisFct, VG_(fnptr_to_fnentry)( f ), argv);
  addStmtToIRSB( sb, IRStmt_Dirty(di) );
}
//-----------------------------------------------------------------
/* instruments a Binary Operation Expression in a Ist_WrTmp statement */
static void instrument_Binop(IRSB* sb, IRStmt* st, IRType type, Addr64 cia) {
  HChar thisFct[]="instrument_Biop";
  IRDirty* di;
  IRExpr** argv;
  IRExpr *op = st->Ist.WrTmp.data;
  IRExpr* oa_event_expr;
  IROp irop=op->Iex.Binop.op;
  void* f=callbackFromIROp(irop);
  if (f == NULL) return;
  OA_InstrumentContext inscon=contextForIop(cia, irop);
  if (not_worth_watching(inscon))
    return;  // filter events that can't be attached to source-code location
  updateStats(inscon->op);
  oa_event_expr = mkIRExpr_HWord( (HWord)inscon );
  IRExpr * args1[2];
  packToI32orI64(sb, op->Iex.Binop.arg1, args1, irop);
  IRExpr * args2[2];
  packToI32orI64(sb, op->Iex.Binop.arg2, args2, irop);
  argv = mkIRExprVec_3(args1[0], args2[0], oa_event_expr);
  di = unsafeIRDirty_0_N( 3, thisFct, VG_(fnptr_to_fnentry)( f ), argv);
  addStmtToIRSB( sb, IRStmt_Dirty(di) );
  if (args1[1] != NULL) {
    // we need a second callback for 64bit types
    argv = mkIRExprVec_3(args1[1], args2[1], oa_event_expr);
    di = unsafeIRDirty_0_N( 3, thisFct, VG_(fnptr_to_fnentry)( f ), argv);
    addStmtToIRSB( sb, IRStmt_Dirty(di) );
  }
}

//-----------------------------------------------------------------
/* instruments a Binary Operation Expression in a Ist_WrTmp statement */
static void instrument_Triop(IRSB* sb, IRStmt* st, Addr64 cia) {
  HChar thisFct[]="instrument_Triop";
  IRDirty* di;
  IRExpr** argv;
  IRExpr *op = st->Ist.WrTmp.data;
  IRExpr* oa_event_expr;
  IROp irop=op->Iex.Triop.details->op;
  void* f=callbackFromIROp(irop);
  if (f == NULL) return;
  OA_InstrumentContext inscon=contextForIop(cia, irop);
  if (not_worth_watching(inscon))
    return;  // filter events that can't be attached to source-code location
  updateStats(inscon->op);
  oa_event_expr = mkIRExpr_HWord( (HWord)inscon );
  IRExpr * args1[2];
  packToI32orI64(sb, op->Iex.Triop.details->arg1, args1, irop);
  IRExpr * args2[2];
  packToI32orI64(sb, op->Iex.Triop.details->arg2, args2, irop);
  IRExpr * args3[2];
  packToI32orI64(sb, op->Iex.Triop.details->arg3, args3, irop);
  argv = mkIRExprVec_4(args1[0], args2[0], args3[0], oa_event_expr);
  di = unsafeIRDirty_0_N( 4, thisFct, VG_(fnptr_to_fnentry)( f ), argv);
  addStmtToIRSB( sb, IRStmt_Dirty(di) );
  if (args2[1] != NULL) {
    // we need a second callback for 64bit types
    argv = mkIRExprVec_4(args1[1], args2[1], args3[1], oa_event_expr);
    di = unsafeIRDirty_0_N( 4, thisFct, VG_(fnptr_to_fnentry)( f ), argv);
    addStmtToIRSB( sb, IRStmt_Dirty(di) );
  }
}

/* Instrument a function call with one F64 as parameter by adding a tmp var
with the param value, and passing it to a dirty call. amd64 only*/
static void instrument_Call_1x_F64(IRSB* sb, Addr64 cia, OA_Call call){
  HChar thisFct[]="instrument_function_call";
  IROp op = Iop_LAST;
  IRExpr* oa_event_expr;
  OA_InstrumentContext inscon=contextForCall(cia, call);
  oa_event_expr = mkIRExpr_HWord( (HWord)inscon );
  void *f=callbackFromOACall(call);
  if (f == NULL) return;
  updateStatsCall(call);
  IRTemp irTemp = newIRTemp(sb->tyenv, Ity_F64);
  IRExpr *get_expr = IRExpr_Get(FP_XMM0_REG, Ity_F64);
  IRStmt *get_stmt = IRStmt_WrTmp(irTemp, get_expr);
  addStmtToIRSB(sb, get_stmt);
  IRExpr *tmp_expr = IRExpr_RdTmp(irTemp);
  IRExpr* args1[2];
  IRExpr** argv;
  IRDirty* di;
  packToI32orI64(sb, tmp_expr, args1, op);
  argv = mkIRExprVec_2(args1[0], oa_event_expr);
  di = unsafeIRDirty_0_N( 2, thisFct, VG_(fnptr_to_fnentry)(f), argv);
  addStmtToIRSB(sb, IRStmt_Dirty(di));
}

/* Instrument a function call with one F32 as parameter by adding a tmp var
with the param value, and passing it to a dirty call. amd64 only*/
static void instrument_Call_1x_F32(IRSB* sb, Addr64 cia, OA_Call call){
  HChar thisFct[]="instrument_function_call";
  IROp op = Iop_LAST;
  IRExpr* oa_event_expr;
  OA_InstrumentContext inscon=contextForCall(cia, call);
  oa_event_expr = mkIRExpr_HWord( (HWord)inscon );
  void *f=callbackFromOACall(call);
  if (f == NULL) return;
  updateStatsCall(call);
  IRTemp irTemp = newIRTemp(sb->tyenv, Ity_F32);
  IRExpr *get_expr = IRExpr_Get(FP_XMM0_REG, Ity_F32);
  IRStmt *get_stmt = IRStmt_WrTmp(irTemp, get_expr);
  addStmtToIRSB(sb, get_stmt);
  IRExpr *tmp_expr = IRExpr_RdTmp(irTemp);
  IRExpr* args1[2];
  IRExpr** argv;
  IRDirty* di;
  packToI32orI64(sb, tmp_expr, args1, op);
  argv = mkIRExprVec_2(args1[0], oa_event_expr);
  di = unsafeIRDirty_0_N( 2, thisFct, VG_(fnptr_to_fnentry)(f), argv);
  addStmtToIRSB(sb, IRStmt_Dirty(di));
}

static void check_need_call_intrumentation(IRSB* sb, Addr64 cia){
  HChar fnname[COJAC_FCT_LEN];
  HChar ifname[COJAC_FCT_LEN];
  if (VG_(get_fnname_if_entry)(cia, fnname, sizeof(fnname))){
    int i;
    for(i = 0; i < OA_CALL_MAX; i++){
      Call_Cojac_attributes cca = oa_all_call_attr[i];
      if(cca.callbackI64 == NULL) continue;
      VG_(strcpy)(ifname, cca.name);
      if(0 == VG_(strcmp)(fnname, ifname)){
        switch(cca.paramType){
          case Call_1xF32: 
            instrument_Call_1x_F32(sb, cia, cca.call);
            break;
          case Call_1xF64: 
            instrument_Call_1x_F64(sb, cia, cca.call);
            break;
          default: break;
        }
      }
    }
  }
}

//-----------------------------------------------------------------
//-----------------------------------------------------------------
//-----------------------------------------------------------------
static void oa_post_clo_init(void) {
  populate_iop_struct();
  populate_call_struct();
  //VG_(message)(Vg_UserMsg, "Nb of ops %d \n", (Iop_Rsqrte32x4-Iop_INVALID));
}

static void oa_print_usage(void) {
  VG_(printf)("    --aggr=no|yes  Reports problems even where file/line cannot be determined [no]\n");
  VG_(printf)("    --i16=yes|no   Watch 16bits int operations [yes]\n");
  VG_(printf)("    --i32=yes|no   Watch 32bits int operations [yes]\n");
  VG_(printf)("    --f32=yes|no   Watch 32bits float operations [yes]\n");
  VG_(printf)("    --f64=yes|no   Watch 64bits double operations [yes]\n");
  VG_(printf)("    --f64_Ulp_Factor=<number>  Ulp factor for cancellation and comparisons on doubles [4.0]\n");
  VG_(printf)("    --f32_Ulp_Factor=<number>  Ulp factor for cancellation and comparisons on floats [4.0]\n");
  VG_(printf)("    --castToI16=yes|no    Watch int to short typecasting [yes]\n");
  VG_(printf)("    --castFromF64=yes|no    Watch float or double to int or long typecasting [yes]\n");
  VG_(printf)("    --stacktrace=<number> Depth of the stacktrace [1] \n");
  VG_(printf)("    --mathStacktrace=<number> Depth of the stacktrace for errors from calls to mathematical functions [2] \n");
  VG_(printf)("    --mathOp=yes|no   Watch for mathematical operations and calls [yes]\n");
}
static void oa_print_debug_usage(void) {
}

static Bool oa_process_cmd_line_option(const HChar* argv) {
  if        (VG_BOOL_CLO(argv, "--aggr",       OA_(options).isAggr)) {
    return True;
  } else if (VG_INT_CLO(argv, "--stacktrace", OA_(options).stacktraceDepth)) {
    return True;
  } else if (VG_INT_CLO(argv, "--mathStacktrace", OA_(options).stacktraceCallDepth)) {
    return True;
  } else if (VG_BOOL_CLO(argv, "--i32", OA_(options).i32)) {
    return True;
  } else if (VG_BOOL_CLO(argv, "--f32", OA_(options).f32)) {
    return True;
  } else if (VG_BOOL_CLO(argv, "--f64", OA_(options).f64)) {
    return True;
  } else if (VG_DBL_CLO(argv, "--f64_Ulp_Factor", OA_(options).Ulp_factor_f64)) {
    return True;
  } else if (VG_DBL_CLO(argv, "--f32_Ulp_Factor", OA_(options).Ulp_factor_f32)) {
    return True;
  } else if (VG_BOOL_CLO(argv, "--i16", OA_(options).i16)) {
    return True;
  } else if (VG_BOOL_CLO(argv, "--i64", OA_(options).i64)) {
    return True;
  } else if (VG_BOOL_CLO(argv, "--castFromF64", OA_(options).castFromF64)) {
    return True;
  } else if (VG_BOOL_CLO(argv, "--castToI16", OA_(options).castToI16)) {
    return True;
  }  else if (VG_BOOL_CLO(argv, "--mathOp", OA_(options).mathOp)) {
    return True;
  }
  return False;
}

//-----------------------------------------------------------------
static void oa_set_default_options(void) {
  OA_(options).castToI16    = True;
  OA_(options).castFromF64  = True;
  OA_(options).Ulp_factor_f64 = 4.0;
  OA_(options).Ulp_factor_f32 = 4.0;
  OA_(options).f32          = True;
  OA_(options).f64          = True;
  OA_(options).i16          = True;
  OA_(options).i32          = True;
  OA_(options).i64          = True;
  OA_(options).mathOp       = True;
  OA_(options).isAggr       = False;
  OA_(options).stacktraceDepth = 1;
  OA_(options).stacktraceCallDepth = 2;
}

//-----------------------------------------------------------------
static IRSB* oa_instrument (VgCallbackClosure* closure,
                            IRSB*              sbIn,
                            VexGuestLayout*    layout,
                            VexGuestExtents*   vge,
                            VexArchInfo *      varch,
                            IRType             gWordTy,
                            IRType             hWordTy ) {
  Int        i;
  IRSB*      sbOut;
  IRType     type;
  Addr64     cia; /* address of current insn */
  IRStmt*    st;

  if (gWordTy != hWordTy) {
    VG_(tool_panic)("host/guest word size mismatch"); // currently unsupported
  }
  thisWordWidth=gWordTy;
  //if (gWordTy != Ity_I32) ppIRType(gWordTy);
  /* Set up SB */
  sbOut = deepCopyIRSBExceptStmts(sbIn);

  // Copy verbatim any IR preamble preceding the first IMark
  i = 0;
  while (i < sbIn->stmts_used && sbIn->stmts[i]->tag != Ist_IMark) {
    addStmtToIRSB( sbOut, sbIn->stmts[i] );
    i++;
  }
  st = sbIn->stmts[i];
  cia   = st->Ist.IMark.addr;
  for (/*use current i*/; i < sbIn->stmts_used; i++) {
    st = sbIn->stmts[i];
    if (!st || st->tag == Ist_NoOp) continue;
    IRExpr* expr;

    switch (st->tag) {
      case Ist_IMark:
        cia   = st->Ist.IMark.addr;  
        check_need_call_intrumentation(sbOut, cia);
        break;
      case Ist_WrTmp:
        // Add a call to trace_load() if --trace-mem=yes.
        expr = st->Ist.WrTmp.data;
        type = typeOfIRExpr(sbOut->tyenv, expr);
        tl_assert(type != Ity_INVALID);
        switch (expr->tag) {
          case Iex_Unop:
              instrument_Unop( sbOut, st, cia );         break;
          case Iex_Binop:
              instrument_Binop( sbOut, st, type, cia );  break;
          case Iex_Triop:
              instrument_Triop( sbOut, st, cia );        break;
          case Iex_Qop: break;
            //instrument_Qop( sbOut, expr, type );     break;
          default: break;
        } // switch
        break;
      default: break;
    } // switch
    addStmtToIRSB( sbOut, st );
  } // for
  return sbOut;
}

//-----------------------------------------------------------------
static void oa_fini(Int exitcode) {
  print_instrumentation_stats();
}
//-----------------------------------------------------------------
static void oa_pre_clo_init(void) {
  oa_set_default_options();
  VG_(details_name)            ("Cojac");
  VG_(details_version)         ("0.1");
  VG_(details_description)     ("the Cojac-grind numerical problem sniffer");
  VG_(details_copyright_author)(
      "Copyright (C) 2011-2014, and GNU GPL'd, by Fred Bapst et al.");
  VG_(details_bug_reports_to)  (VG_BUGS_TO);

  VG_(basic_tool_funcs)        (
      oa_post_clo_init,
      oa_instrument,
      oa_fini);

  VG_(needs_core_errors)       ();

  VG_(needs_tool_errors)       (
      OA_(eq_Error),
      OA_(before_pp_Error),
      OA_(pp_Error),
      True,/*show TIDs for errors*/
      OA_(update_Error_extra),
      OA_(is_recognised_suppression),
      OA_(read_extra_suppression_info),
      OA_(error_matches_suppression),
      OA_(get_error_name),
      OA_(get_extra_suppression_info),
      OA_(get_extra_suppression_use),
      OA_(update_extra_suppression_use)
  );

  VG_(needs_command_line_options) (
      oa_process_cmd_line_option,
      oa_print_usage,
      oa_print_debug_usage
  );

  VG_(needs_var_info)();
  /* No other needs, no core events to track */
}

/*--------------------------------------------------------------------*/
VG_DETERMINE_INTERFACE_VERSION(oa_pre_clo_init)

/*
./vg-in-place --trace-flags=00100000 --trace-notbelow=0 --tool=cojac HelloCojac 2>a.txt
 */


/*--------------------------------------------------------------------*/
/*--- end                                                          ---*/
/*--------------------------------------------------------------------*/
