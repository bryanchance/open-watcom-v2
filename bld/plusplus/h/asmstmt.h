/****************************************************************************
*
*                            Open Watcom Project
*
* Copyright (c) 2002-2022 The Open Watcom Contributors. All Rights Reserved.
*    Portions Copyright (c) 1983-2002 Sybase, Inc. All Rights Reserved.
*
*  ========================================================================
*
*    This file contains Original Code and/or Modifications of Original
*    Code as defined in and that are subject to the Sybase Open Watcom
*    Public License version 1.0 (the 'License'). You may not use this file
*    except in compliance with the License. BY USING THIS FILE YOU AGREE TO
*    ALL TERMS AND CONDITIONS OF THE LICENSE. A copy of the License is
*    provided with the Original Code and Modifications, and is also
*    available at www.sybase.com/developer/opensource.
*
*    The Original Code and all software distributed under the License are
*    distributed on an 'AS IS' basis, WITHOUT WARRANTY OF ANY KIND, EITHER
*    EXPRESS OR IMPLIED, AND SYBASE AND ALL CONTRIBUTORS HEREBY DISCLAIM
*    ALL SUCH WARRANTIES, INCLUDING WITHOUT LIMITATION, ANY WARRANTIES OF
*    MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE, QUIET ENJOYMENT OR
*    NON-INFRINGEMENT. Please see the License for the specific language
*    governing rights and limitations under the License.
*
*  ========================================================================
*
* Description:  WHEN YOU FIGURE OUT WHAT THIS FILE DOES, PLEASE
*               DESCRIBE IT HERE!
*
****************************************************************************/


#ifndef _ASMSTMT_H
#define _ASMSTMT_H

#include "vbuf.h"
#include "asalloc.h"
#if _INTEL_CPU
  #include "asminlin.h"
#else /* _RISC_CPU */
  #include "asinline.h"
#endif

#define MAX_INSTR_SIZE  64

extern PTREE        AsmStmt( void );

// from CPRAGxxx

extern bool         AsmInsertFixups( VBUF *code );
extern void         AsmUsesAuto( void );
extern void         AsmSysInit( void );
extern void         AsmSysFini( void );
extern char const   *AsmSysDefineByte( void );
extern void         AsmSysCopyCode( void );
extern void         AsmSysLine( const char * );

#endif
