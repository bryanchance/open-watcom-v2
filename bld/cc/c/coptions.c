/****************************************************************************
*
*                            Open Watcom Project
*
* Copyright (c) 2002-2023 The Open Watcom Contributors. All Rights Reserved.
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
* Description:  C compiler command line option processing.
*
****************************************************************************/


/*****************************************************************************
*                                                                            *
*       If you add an option, don't forget to change bld/cc/gml/options.gml  *
*       Also, don't forget to add a case in MacroDefs                        *
*       to predefine a __SW_xx macro                                         *
*                                                                            *
*****************************************************************************/


#include "cvars.h"
#include <ctype.h>
#include "wio.h"
#include "watcom.h"
#include "pdefn2.h"
#include "cgdefs.h"
#include "cgswitch.h"
#include "iopath.h"
#include "pathlist.h"
#include "toggles.h"
#include "feprotos.h"

#include "clibext.h"


#define __isdigit(c)    ((c) >= '0' && (c) <= '9')

#define PEGGED( r ) boolbit     peg_##r##s_used : 1; \
                    boolbit     peg_##r##s_on   : 1

#define SET_PEGGED( r, b )          SwData.peg_##r##s_used = true; SwData.peg_##r##s_on = b;
#define CHECK_SET_PEGGED( r, b )    if( !SwData.peg_##r##s_used ) { SwData.peg_##r##s_used = true; SwData.peg_##r##s_on = b; }
#define CHECK_TO_PEGGED( r )        if( !SwData.peg_##r##s_used ) SwData.peg_##r##s_on = true;

#define INC_VAR "INCLUDE"

#define MAX_NESTING 32

enum encoding {
    ENC_ZK = 1,
    ENC_ZK0,
    ENC_ZK1,
    ENC_ZK2,
    ENC_ZK3,
    ENC_ZKL,
    ENC_ZKU,
    ENC_ZK0U
};

struct  option {
    char        *option;
    unsigned    value;
    void        (*function)(void);
};

static unsigned     OptValue;
static const char   *OptScanPtr;
static const char   *OptParm;

static struct
{
    char        *sys_name;

    /*
     * TARGET CPU SUPPORT - Intel defined (AXP/PPC uses CPU0 as default CPU)
     */
    enum {
        SW_CPU_DEF,     /*  No target CPU specified     */
        SW_CPU0,        /*  Target 8086/8               */
        SW_CPU1,        /*  Target 80186/8              */
        SW_CPU2,        /*  Target 80286                */
        SW_CPU3,        /*  Target 80386                */
        SW_CPU4,        /*  Target 80486                */
        SW_CPU5,        /*  Target Pentium              */
        SW_CPU6         /*  Target Pentium-Pro          */
    } cpu;
    /*
     * TARGET FPU SUPPORT
     */
    enum {
        SW_FPU_DEF,     /*  No target FPU specified     */
        SW_FPU0,        /*  Target 8087 co-pro          */
        SW_FPU3,        /*  Target 80387 co-pro         */
        SW_FPU5,        /*  Target Pentium int fpu      */
        SW_FPU6         /*  Target Pentium-Pro int fpu  */
    } fpu;
    /*
     * FPU CALL TYPES
     */
    enum {
        SW_FPT_DEF,     /*  No FPU call type specified  */
        SW_FPT_CALLS,   /*  FPU calls via library       */
        SW_FPT_EMU,     /*  FPU calls inline & emulated */
        SW_FPT_INLINE   /*  FPU calls inline            */
    } fpt;
    /*
     * MEMORY MODELS
     */
    enum {
        SW_M_DEF,       /*  No memory model specified   */
        SW_MF,          /*  Flat memory model           */
        SW_MS,          /*  Small memory model          */
        SW_MM,          /*  Medium memory model         */
        SW_MC,          /*  Compact memory model        */
        SW_ML,          /*  Large memory model          */
        SW_MH           /*  Huge memory model           */
    } mem;
    /*
     * DEBUGGING INFORMATION TYPE
     */
    enum {
        SW_DF_DEF,      /*  No debug type specified     */
        SW_DF_WATCOM,   /*  Use Watcom                  */
        SW_DF_CV,       /*  Use CodeView                */
        SW_DF_DWARF,    /*  Use DWARF                   */
        SW_DF_DWARF_A,  /*  Use DWARF + A?              */
        SW_DF_DWARF_G   /*  Use DWARF + G?              */
    } dbg_fmt;

    PEGGED( d );
    PEGGED( e );
    PEGGED( f );
    PEGGED( g );
    boolbit     nd_used : 1;
} SwData;

/*
 * local variables
 */
static bool     debug_optimization_change = false;
static int      character_encoding = 0;
static unsigned unicode_CP = 0;

bool EqualChar( int c )
{
    return( c == '#'
        || c == '=' );
}

static void DefSwitchMacro( const char *str )
{
    char buff[64];

    Define_Macro( strcpy( strcpy( buff, "__SW_" ) + 5, str ) - 5 );
}

static void SetCharacterEncoding( void )
{
    CompFlags.jis_to_unicode = false;

    switch( character_encoding ) {
    case ENC_ZKU:
        LoadUnicodeTable( unicode_CP );
        break;
    case ENC_ZK0U:
        CompFlags.use_unicode = false;
        SetDBChar( 0 );                     /* set double-byte char type */
        CompFlags.jis_to_unicode = true;
        break;
    case ENC_ZK:
    case ENC_ZK0:
        CompFlags.use_unicode = false;
        SetDBChar( 0 );                     /* set double-byte char type */
        break;
    case ENC_ZK1:
        CompFlags.use_unicode = false;
        SetDBChar( 1 );                     /* set double-byte char type */
        break;
    case ENC_ZK2:
        CompFlags.use_unicode = false;
        SetDBChar( 2 );                     /* set double-byte char type */
        break;
    case ENC_ZK3:
        CompFlags.use_unicode = false;
        SetDBChar( 3 );                     /* set double-byte char type */
        break;
    case ENC_ZKL:
        CompFlags.use_unicode = false;
        SetDBChar( -1 );                    /* set double-byte char type to default */
        break;
    }
}

static void SetTargetName( const char *name )
{
    if( SwData.sys_name != NULL ) {
        CMemFree( SwData.sys_name );
    }
    SwData.sys_name = CMemStrDup( name );
}

static void SetTargetSystem( void )
{
    if( SwData.sys_name == NULL ) {
#if _INTEL_CPU
    #if defined( __NOVELL__ )
        SetTargetName( "NETWARE" );
    #elif defined( __QNX__ )
        SetTargetName( "QNX" );
    #elif defined( __LINUX__ )
        SetTargetName( "LINUX" );
    #elif defined( __HAIKU__ )
        SetTargetName( "HAIKU" );
    #elif defined( __SOLARIS__ ) || defined( __sun__ )
        SetTargetName( "SOLARIS" );
    #elif defined( __OSX__ ) || defined( __APPLE__ )
        SetTargetName( "OSX" );
    #elif defined( __OS2__ )
        SetTargetName( "OS2" );
    #elif defined( __NT__ )
        SetTargetName( "NT" );
    #elif defined( __DOS__ )
        SetTargetName( "DOS" );
    #elif defined( __BSD__ )
        SetTargetName( "BSD" );
    #elif defined( __RDOS__ )
        SetTargetName( "RDOS" );
    #else
        #error "Target OS not defined"
    #endif
#elif _RISC_CPU
        /*
         * we only have NT libraries for Alpha right now
         */
        SetTargetName( "NT" );
#elif _CPU == _SPARC
        SetTargetName( "SOLARIS" );
#else
    #error Target Machine OS not configured
#endif
    }

    TargetSystem = TS_OTHER;
    if( SwData.sys_name != NULL ) {
        if( strcmp( SwData.sys_name, "DOS" ) == 0 ) {
            TargetSystem = TS_DOS;
        } else if( strcmp( SwData.sys_name, "NETWARE" ) == 0 ) {
            TargetSystem = TS_NETWARE;
        } else if( strcmp( SwData.sys_name, "NETWARE5" ) == 0 ) {
            TargetSystem = TS_NETWARE5;
            SetTargetName( "NETWARE" );
        } else if( strcmp( SwData.sys_name, "WINDOWS" ) == 0 ) {
            TargetSystem = TS_WINDOWS;
        } else if( strcmp( SwData.sys_name, "CHEAP_WINDOWS" ) == 0 ) {
#if _CPU == 8086
            TargetSystem = TS_CHEAP_WINDOWS;
#else
            TargetSystem = TS_WINDOWS;
#endif
            SetTargetName( "WINDOWS" );
        } else if( strcmp( SwData.sys_name, "NT" ) == 0 ) {
            TargetSystem = TS_NT;
        } else if( strcmp( SwData.sys_name, "LINUX" ) == 0 ) {
            TargetSystem = TS_LINUX;
        } else if( strcmp( SwData.sys_name, "QNX" ) == 0 ) {
            TargetSystem = TS_QNX;
        } else if( strcmp( SwData.sys_name, "OS2" ) == 0 ) {
            TargetSystem = TS_OS2;
        } else if( strcmp( SwData.sys_name, "RDOS" ) == 0 ) {
            TargetSystem = TS_RDOS;
        } else if( strcmp( SwData.sys_name, "HAIKU" ) == 0
                || strcmp( SwData.sys_name, "OSX" ) == 0
                || strcmp( SwData.sys_name, "SOLARIS" ) == 0
                || strcmp( SwData.sys_name, "BSD" ) == 0 ) {
            TargetSystem = TS_UNIX;
        }
    }
}

static void SetFinalTargetSystem( void )
{
    if( CompFlags.non_iso_compliant_names_enabled ) {
#if _CPU == 8086
        PreDefine_Macro( "M_I86" );
#elif _CPU == 386
        PreDefine_Macro( "M_I386" );
#elif _CPU == _AXP
        PreDefine_Macro( "M_ALPHA" );
#elif _CPU == _PPC
        PreDefine_Macro( "M_PPC" );
#elif _CPU == _MIPS
        PreDefine_Macro( "M_MRX000" );
#elif _CPU == _SPARC
        PreDefine_Macro( "M_SPARC" );
#endif
    }
#if _CPU == 8086
    PreDefine_Macro( "_M_I86" );
    PreDefine_Macro( "__I86__" );
    PreDefine_Macro( "__X86__" );
    PreDefine_Macro( "_X86_" );
#elif _CPU == 386
    PreDefine_Macro( "_M_I386" );
    PreDefine_Macro( "__386__" );
    PreDefine_Macro( "__X86__" );
    PreDefine_Macro( "_X86_" );
    PreDefine_Macro( "_STDCALL_SUPPORTED" );
#elif _CPU == _AXP
    PreDefine_Macro( "_M_ALPHA" );
    PreDefine_Macro( "__ALPHA__" );
    PreDefine_Macro( "_ALPHA_" );
    PreDefine_Macro( "__AXP__" );
    PreDefine_Macro( "_STDCALL_SUPPORTED" );
#elif _CPU == _PPC
    PreDefine_Macro( "_M_PPC" );
    PreDefine_Macro( "__POWERPC__" );
    PreDefine_Macro( "__PPC__" );
    PreDefine_Macro( "_PPC_" );
#elif _CPU == _MIPS
    PreDefine_Macro( "_M_MRX000" );
    PreDefine_Macro( "__MIPS__" );
#elif _CPU == _SPARC
    PreDefine_Macro( "_M_SPARC" );
    PreDefine_Macro( "__SPARC__" );
    PreDefine_Macro( "_SPARC_" );
#else
    #error SetTargetSystem not configured
#endif

    PreDefine_Macro( "__WATCOM_INT64__" );
    PreDefine_Macro( "_INTEGRAL_MAX_BITS=64" );

    if( SwData.sys_name != NULL ) {
        char *buff = CMemAlloc( 2 + strlen( SwData.sys_name ) + 2 + 1 );
        sprintf( buff, "__%s__", SwData.sys_name );
        PreDefine_Macro( buff );
        CMemFree( buff );
    }

    switch( TargetSystem ) {
    case TS_DOS:
        if( CompFlags.non_iso_compliant_names_enabled ) {
            PreDefine_Macro( "MSDOS" );
        }
        PreDefine_Macro( "_DOS" );
        break;
#if _CPU == 386
    case TS_NETWARE5:
        PreDefine_Macro( "__NETWARE5__" );
        /* fall through */
    case TS_NETWARE:
        PreDefine_Macro( "__NETWARE_386__" );
        /*
         * If using NetWare, set Stack87 unless the target
         * is NetWare 5 or higher.
         */
        if( TargetSystem == TS_NETWARE ) {
            Stack87 = 4;
        }
        if( SwData.mem == SW_M_DEF ) {
            SwData.mem = SW_MS;
        }
        /*
         * NETWARE uses stack based calling conventions
         * by default - silly people.
         */
        CompFlags.register_conventions = false;
        break;
    case TS_RDOS:
        PreDefine_Macro( "_RDOS" );
        break;
    case TS_QNX:
        PreDefine_Macro( "__UNIX__" );
        break;
    case TS_CHEAP_WINDOWS:
        PreDefine_Macro( "__CHEAP_WINDOWS__" );
        /* fall through */
    case TS_WINDOWS:
  #if _CPU == 8086
        PreDefine_Macro( "_WINDOWS" );
        TargetSwitches |= CGSW_X86_WINDOWS | CGSW_X86_CHEAP_WINDOWS;
        CHECK_SET_PEGGED( d, true )
  #else
        PreDefine_Macro( "__WINDOWS_386__" );
        CHECK_SET_PEGGED( f, false )
        switch( SwData.fpt ) {
        case SW_FPT_DEF:
        case SW_FPT_EMU:
            SwData.fpt = SW_FPT_INLINE;
            break;
        default:
            break;
        }
  #endif
        break;
#endif
    case TS_NT:
        PreDefine_Macro( "_WIN32" );
        break;
    case TS_LINUX:
    case TS_UNIX:
        PreDefine_Macro( "__UNIX__" );
        break;
    case TS_OTHER:
        break;
    }

#if _RISC_CPU
    if( (GenSwitches & (CGSW_GEN_OBJ_ELF | CGSW_GEN_OBJ_COFF)) == 0 ) {
        if( TargetSystem == TS_NT ) {
            GenSwitches |= CGSW_GEN_OBJ_COFF;
        } else {
            GenSwitches |= CGSW_GEN_OBJ_ELF;
        }
    }
#endif
}

static void SetGenSwitches( void )
{
#if _INTEL_CPU
  #if _CPU == 8086
    if( SwData.cpu == SW_CPU_DEF )
        SwData.cpu = SW_CPU0;
    if( SwData.fpu == SW_FPU_DEF )
        SwData.fpu = SW_FPU0;
    if( SwData.mem == SW_M_DEF )
        SwData.mem = SW_MS;
    CHECK_TO_PEGGED( f );
    CHECK_TO_PEGGED( g );
  #else
    if( SwData.cpu == SW_CPU_DEF )
        SwData.cpu = SW_CPU6;
    if( SwData.fpu == SW_FPU_DEF )
        SwData.fpu = SW_FPU3;
    if( SwData.mem == SW_M_DEF )
        SwData.mem = SW_MF;
    TargetSwitches |= CGSW_X86_USE_32;
  #endif
    switch( SwData.fpu ) {
    case SW_FPU0:
        SET_FPU_LEVEL( ProcRevision, FPU_87 );
        break;
    case SW_FPU3:
        SET_FPU_LEVEL( ProcRevision, FPU_387 );
        break;
    case SW_FPU5:
        SET_FPU_LEVEL( ProcRevision, FPU_586 );
        break;
    case SW_FPU6:
        SET_FPU_LEVEL( ProcRevision, FPU_686 );
        break;
    default:
        break;
    }
    switch( SwData.fpt ) {
    case SW_FPT_DEF:
    case SW_FPT_EMU:
        SwData.fpt = SW_FPT_EMU;
        SET_FPU_EMU( ProcRevision );
        break;
    case SW_FPT_INLINE:
        SET_FPU_INLINE( ProcRevision );
        break;
    case SW_FPT_CALLS:
        SET_FPU( ProcRevision, FPU_NONE );
        break;
    }
    SET_CPU( ProcRevision, SwData.cpu - SW_CPU0 + CPU_86 );
    switch( SwData.mem ) {
    case SW_MF:
        TargetSwitches |= CGSW_X86_FLAT_MODEL | CGSW_X86_CHEAP_POINTER;
        CHECK_TO_PEGGED( d );
        CHECK_TO_PEGGED( e );
        CHECK_TO_PEGGED( f );
    case SW_MS:
        TargetSwitches |= CGSW_X86_CHEAP_POINTER;
        CHECK_TO_PEGGED( d );
        break;
    case SW_MM:
        TargetSwitches |= CGSW_X86_BIG_CODE | CGSW_X86_CHEAP_POINTER;
        CHECK_TO_PEGGED( d );
        break;
    case SW_MC:
        TargetSwitches |= CGSW_X86_BIG_DATA | CGSW_X86_CHEAP_POINTER;
        break;
    case SW_ML:
        TargetSwitches |= CGSW_X86_BIG_CODE | CGSW_X86_BIG_DATA | CGSW_X86_CHEAP_POINTER;
        break;
    case SW_MH:
        TargetSwitches |= CGSW_X86_BIG_CODE | CGSW_X86_BIG_DATA;
        break;
    default:
        break;
    }
    if( !SwData.peg_ds_on )
        TargetSwitches |= CGSW_X86_FLOATING_DS;
    if( !SwData.peg_es_on )
        TargetSwitches |= CGSW_X86_FLOATING_ES;
    if( !SwData.peg_fs_on )
        TargetSwitches |= CGSW_X86_FLOATING_FS;
    if( !SwData.peg_gs_on )
        TargetSwitches |= CGSW_X86_FLOATING_GS;
#endif
    switch( SwData.dbg_fmt ) {
    case SW_DF_WATCOM:
        /*
         * nothing to do
         */
        break;
    case SW_DF_CV:
        GenSwitches |= CGSW_GEN_DBG_CV;
        break;
    case SW_DF_DEF:
        /*
         * DWARF is the default
         */
    case SW_DF_DWARF:
        GenSwitches |= CGSW_GEN_DBG_DF;
        break;
    case SW_DF_DWARF_A:
        GenSwitches |= CGSW_GEN_DBG_DF | CGSW_GEN_DBG_PREDEF;
        SymDFAbbr = SpcSymbol( "__DFABBREV", GetType( TYP_USHORT ), SC_EXTERN );
        break;
    case SW_DF_DWARF_G:
        GenSwitches |= CGSW_GEN_DBG_DF | CGSW_GEN_DBG_PREDEF;
        SymDFAbbr = SpcSymbol( "__DFABBREV", GetType( TYP_USHORT ), SC_NONE );
        break;
    }
}

static void MacroDefs( void )
{
    if( GenSwitches & CGSW_GEN_I_MATH_INLINE ) {
        DefSwitchMacro( "OM" );
    }
#if _INTEL_CPU
  #if _CPU == 8086
    #define MX86 "M_I86"
  #else
    #define MX86 "M_386"
  #endif
    if( CompFlags.non_iso_compliant_names_enabled ) {
        switch( SwData.mem ) {
        case SW_MS:
            Define_Macro( MX86 "SM" );
            break;
        case SW_MM:
            Define_Macro( MX86 "MM" );
            break;
        case SW_MC:
            Define_Macro( MX86 "CM" );
            break;
        case SW_ML:
            Define_Macro( MX86 "LM" );
            break;
        case SW_MH:
            Define_Macro( MX86 "HM" );
            break;
        case SW_MF:
            Define_Macro( MX86 "FM" );
            break;
        default:
            break;
        }
    }
  #if _CPU == 8086
    #define X86 "_M_I86"
  #else
    #define X86 "_M_386"
  #endif
    switch( SwData.mem ) {
    case SW_MS:
        DefSwitchMacro( "MS" );
        Define_Macro( X86 "SM" );
        Define_Macro( "__SMALL__" );
        break;
    case SW_MM:
        DefSwitchMacro( "MM" );
        Define_Macro( X86 "MM" );
        Define_Macro( "__MEDIUM__" );
        break;
    case SW_MC:
        DefSwitchMacro( "MC" );
        Define_Macro( X86 "CM" );
        Define_Macro( "__COMPACT__" );
        break;
    case SW_ML:
        DefSwitchMacro( "ML" );
        Define_Macro( X86 "LM" );
        Define_Macro( "__LARGE__" );
        break;
    case SW_MH:
        DefSwitchMacro( "MH" );
        Define_Macro( X86 "HM" );
        Define_Macro( "__HUGE__" );
        break;
    case SW_MF:
        DefSwitchMacro( "MF" );
        Define_Macro( X86 "FM" );
        Define_Macro( "__FLAT__" );
        break;
    default:
        break;
    }
    if( TargetSwitches & CGSW_X86_FLOATING_FS ) {
        DefSwitchMacro( "ZFF" );
    } else {
        DefSwitchMacro( "ZFP" );
    }
    if( TargetSwitches & CGSW_X86_FLOATING_GS ) {
        DefSwitchMacro( "ZGF" );
    } else {
        DefSwitchMacro( "ZGP" );
    }
    if( TargetSwitches & CGSW_X86_FLOATING_DS ) {
        DefSwitchMacro( "ZDF" );
    } else {
        DefSwitchMacro( "ZDP" );
    }
    if( TargetSwitches & CGSW_X86_FLOATING_SS ) {
        DefSwitchMacro( "ZU" );
    }
    if( TargetSwitches & CGSW_X86_INDEXED_GLOBALS ) {
        DefSwitchMacro( "XGV" );
    }
    if( TargetSwitches & CGSW_X86_WINDOWS ) {
        DefSwitchMacro( "ZW" );
    }
    if( TargetSwitches & CGSW_X86_NEED_STACK_FRAME ) {
        DefSwitchMacro( "OF" );
    }
    if( TargetSwitches & CGSW_X86_GEN_FWAIT_386 ) {
        DefSwitchMacro( "ZFW" );
    }
#endif
#if _RISC_CPU
    if( GenSwitches & CGSW_GEN_OBJ_ENDIAN_BIG ) {
        Define_Macro( "__BIG_ENDIAN__" );
    }
#endif
    if( GenSwitches & CGSW_GEN_NO_CALL_RET_TRANSFORM ) {
        DefSwitchMacro( "OC" );
    }
    if( GenSwitches & CGSW_GEN_SUPER_OPTIMAL ) {
        DefSwitchMacro( "OH" );
    }
    if( GenSwitches & CGSW_GEN_FLOW_REG_SAVES ) {
        DefSwitchMacro( "OK" );
    }
    if( GenSwitches & CGSW_GEN_NO_OPTIMIZATION ) {
        DefSwitchMacro( "OD" );
    }
    if( GenSwitches & CGSW_GEN_RELAX_ALIAS ) {
        DefSwitchMacro( "OA" );
    }
    if( GenSwitches & CGSW_GEN_LOOP_OPTIMIZATION ) {
        DefSwitchMacro( "OL" );
    }
    if( GenSwitches & CGSW_GEN_INS_SCHEDULING ) {
        DefSwitchMacro( "OR" );
    }
    if( GenSwitches & CGSW_GEN_FP_UNSTABLE_OPTIMIZATION ) {
        DefSwitchMacro( "ON" );
    }
    if( GenSwitches & CGSW_GEN_FPU_ROUNDING_OMIT ) {
        DefSwitchMacro( "ZRO" );
    }
    if( GenSwitches & CGSW_GEN_FPU_ROUNDING_INLINE ) {
        DefSwitchMacro( "ZRI" );
    }
    if( CompFlags.use_long_double ) {
        DefSwitchMacro( "FLD" );
    }
    if( CompFlags.signed_char ) {
        DefSwitchMacro( "J" );
    }
    if( PCH_FileName != NULL ) {
        DefSwitchMacro( "FH" );
    }
    if( CompFlags.no_pch_warnings ) {
        DefSwitchMacro( "FHQ" );
    }
    if( CompFlags.inline_functions ) {
        DefSwitchMacro( "OI" );
    }
    if( CompFlags.unique_functions ) {
        DefSwitchMacro( "OU" );
    }
#if _CPU == 386
    if( CompFlags.register_conventions ) {
        DefSwitchMacro( "3R" );
    } else {
        DefSwitchMacro( "3S" );
    }
#endif
    if( CompFlags.emit_names ) {
        DefSwitchMacro( "EN" );
    }
    if( CompFlags.make_enums_an_int ) {
        DefSwitchMacro( "EI" );
    }
    if( CompFlags.zc_switch_used ) {
        DefSwitchMacro( "ZC" );
    }
    if( !CompFlags.use_unicode ) {
        DefSwitchMacro( "ZK" );
    }
#if _INTEL_CPU
    if( CompFlags.save_restore_segregs ) {
        DefSwitchMacro( "R" );
    }
    if( CompFlags.sg_switch_used ) {
        DefSwitchMacro( "SG" );
    }
    if( CompFlags.st_switch_used ) {
        DefSwitchMacro( "ST" );
    }
    if( CompFlags.zu_switch_used ) {
        DefSwitchMacro( "ZU" );
    }
#endif
    if( CompFlags.bm_switch_used ) {
        DefSwitchMacro( "BM" );
        Define_Macro( "_MT" );
    }
    if( CompFlags.bd_switch_used ) {
        DefSwitchMacro( "BD" );
    }
    if( CompFlags.bc_switch_used ) {
        DefSwitchMacro( "BC" );
    }
    if( CompFlags.bg_switch_used ) {
        DefSwitchMacro( "BG" );
    }
    if( CompFlags.br_switch_used ) {
        DefSwitchMacro( "BR" );
        Define_Macro( "_DLL" );
    }
    if( CompFlags.bw_switch_used ) {
        DefSwitchMacro( "BW" );
    }
    if( CompFlags.zm_switch_used ) {
        DefSwitchMacro( "ZM" );
    }
    if( CompFlags.ep_switch_used ) {
        DefSwitchMacro( "EP" );
    }
    if( CompFlags.ee_switch_used ) {
        DefSwitchMacro( "EE" );
    }
    if( CompFlags.ec_switch_used ) {
        DefSwitchMacro( "EC" );
    }
#if _INTEL_CPU
    switch( GET_CPU( ProcRevision ) ) {
    case CPU_86:
        DefSwitchMacro( "0" );
        PreDefine_Macro( "_M_IX86=0" );
        break;
    case CPU_186:
        DefSwitchMacro( "1" );
        PreDefine_Macro( "_M_IX86=100" );
        break;
    case CPU_286:
        DefSwitchMacro( "2" );
        PreDefine_Macro( "_M_IX86=200" );
        break;
    case CPU_386:
        DefSwitchMacro( "3" );
        PreDefine_Macro( "_M_IX86=300" );
        break;
    case CPU_486:
        DefSwitchMacro( "4" );
        PreDefine_Macro( "_M_IX86=400" );
        break;
    case CPU_586:
        DefSwitchMacro( "5" );
        PreDefine_Macro( "_M_IX86=500" );
        break;
    case CPU_686:
        DefSwitchMacro( "6" );
        PreDefine_Macro( "_M_IX86=600" );
        break;
    }
    switch( SwData.fpt ) {
    case SW_FPT_CALLS:
        CompFlags.op_switch_used = false;
        DefSwitchMacro( "FPC" );
        break;
    case SW_FPT_EMU:
        DefSwitchMacro( "FPI" );
        Define_Macro( "__FPI__" );
        break;
    case SW_FPT_INLINE:
        DefSwitchMacro( "FPI87" );
        Define_Macro( "__FPI__" );
        break;
    default:
        break;
    }
    switch( GET_FPU_LEVEL( ProcRevision ) ) {
    case FPU_NONE:
        break;
    case FPU_87:
        DefSwitchMacro( "FP2" );
        break;
    case FPU_387:
        DefSwitchMacro( "FP3" );
        break;
    case FPU_586:
        DefSwitchMacro( "FP5" );
        break;
    case FPU_686:
        DefSwitchMacro( "FP6" );
        break;
    }
    if( SwData.nd_used ) {
        DefSwitchMacro( "ND" );
    }
    if( CompFlags.op_switch_used ) {
        DefSwitchMacro( "OP" );
    }
#endif
    if( !TOGGLE( check_stack ) ) {
        DefSwitchMacro( "S" );
    }
}

static void AddIncList( const char *path_list )
{
    size_t      old_len;
    size_t      len;
    char        *old_list;
    char        *p;

    if( path_list != NULL
      && *path_list != '\0' ) {
        len = strlen( path_list );
        old_list = IncPathList;
        old_len = strlen( old_list );
        IncPathList = CMemAlloc( old_len + 1 + len + 1 );
        strcpy( IncPathList, old_list );
        CMemFree( old_list );
        p = IncPathList + old_len;
        while( *path_list != '\0' ) {
            if( p != IncPathList )
                *p++ = PATH_LIST_SEP;
            path_list = GetPathElement( path_list, NULL, &p );
        }
        *p = '\0';
    }
}

void MergeInclude( void )
/************************
 * must be called after GenCOptions to get req'd IncPathList
 */
{
    const char  *env_var;
    char        *buff;

    if( !CompFlags.cpp_ignore_env ) {
        buff = CMemAlloc( strlen( SwData.sys_name ) + LENLIT( "_" INC_VAR ) + 1 );
        sprintf( buff, "%s_" INC_VAR, SwData.sys_name );
        env_var = FEGetEnv( buff );
        CMemFree( buff );
        AddIncList( env_var );

#if _CPU == 386
        env_var = FEGetEnv( "INC386" );
        if( env_var == NULL ) {
            env_var = FEGetEnv( INC_VAR );
        }
#else
        env_var = FEGetEnv( INC_VAR );
#endif
        AddIncList( env_var );
    }
    SetTargetName( NULL );
}


static bool OptionDelimiter( char c )
{
    return( c == ' '
        || c == '-'
        || c == '\0'
        || c == '\t'
        || c == SwitchChar );
}

static void EnsureEndOfSwitch( void )
{
    char        c;

    if( !OptionDelimiter( *OptScanPtr ) ) {
        for( ;; ) {         /* find start of switch */
            c = *OptScanPtr;
            if( c == '-'
              || c == SwitchChar )
                break;
            --OptScanPtr;
        }
        OptScanPtr = BadCmdLine( ERR_INVALID_OPTION, OptScanPtr );
    }
}
static void StripQuotes( char *fname )
{
    char    *s;
    char    *d;
    char    c;

    if( *fname == '"' ) {
        /*
         * string will shrink so we can reduce in place
         */
        d = fname;
        for( s = fname + 1; (c = *s++) != '\0'; ) {
            if( c == '"' )
                break;
            /*
             * collapse double backslashes, only then look for escaped quotes
             */
            if( c == '\\' ) {
                if( *s == '\\'
                  || *s == '"' ) {
                    c = *s++;
                }
            }
            *d++ = c;
        }
        *d = '\0';
    }
}
static char *CopyOfParm( void )
{
    return( ToStringDup( OptParm, OptScanPtr - OptParm ) );
}
static char *GetAFileName( void )
{
    char    *fname;

    fname = CopyOfParm();
    StripQuotes( fname );
    return( fname );
}

#if _CPU == _AXP
static void SetStructPack( void )   { CompFlags.align_structs_on_qwords = true; }
#endif

static void Set_ZP( void )          { SetPackAmount( OptValue ); }
static void Set_DbgFmt( void )      { SwData.dbg_fmt = OptValue; }

#if _INTEL_CPU
static void SetCPU( void )          { SwData.cpu = OptValue; }
  #if _CPU == 386
static void SetCPU_xR( void )       { SwData.cpu = OptValue; CompFlags.register_conventions = true; }
static void SetCPU_xS( void )       { SwData.cpu = OptValue; CompFlags.register_conventions = false; }
  #endif
static void SetFPU( void )          { SwData.fpu = OptValue; }
static void Set_FPR( void )         { Stack87 = 4; }
static void Set_FPI87( void )       { SwData.fpt = SW_FPT_INLINE; }
static void Set_Emu( void )         { SwData.fpt = SW_FPT_EMU; }
static void Set_FPC( void )         { SwData.fpt = SW_FPT_CALLS; }
static void Set_FPD( void )         { TargetSwitches |= CGSW_X86_P5_DIVIDE_CHECK; }

static void SetMemoryModel( void )  { SwData.mem = OptValue; }
#endif

static void Set_BD( void )          { CompFlags.bd_switch_used = true; GenSwitches |= CGSW_GEN_DLL_RESIDENT_CODE; }
static void Set_BC( void )          { CompFlags.bc_switch_used = true; }
static void Set_BG( void )          { CompFlags.bg_switch_used = true; }
static void Set_BM( void )          { CompFlags.bm_switch_used = true; }

#if _CPU != 8086
static void Set_BR( void )          { CompFlags.br_switch_used = true; }
#endif

static void Set_BW( void )          { CompFlags.bw_switch_used = true; }
static void Set_BT( void )
{
    char    *p;

    p = CopyOfParm();
    SetTargetName( strupr( p ) );
    CMemFree( p );
}

static void SetExtendedDefines( void )
{
    CompFlags.extended_defines = true;
    EnsureEndOfSwitch();
}
static void SetBrowserInfo( void )  { CompFlags.emit_browser_info = true; }

#if _CPU == _AXP
static void Set_AS( void )
{
    TargetSwitches |= CGSW_RISC_ALIGNED_SHORT;
}
#endif

static void Set_AA( void )          { CompFlags.auto_agg_inits = true; }
static void Set_AI( void )          { CompFlags.no_check_inits = true; }
static void Set_AQ( void )          { CompFlags.no_check_qualifiers = true; }
static void Set_D0( void )
{
    debug_optimization_change = false;
    GenSwitches &= ~(CGSW_GEN_DBG_NUMBERS | CGSW_GEN_DBG_TYPES | CGSW_GEN_DBG_LOCALS);
    CompFlags.debug_info_some = false;
    CompFlags.no_debug_type_names = false;
    EnsureEndOfSwitch();
}
static void Set_D1( void )
{
    GenSwitches |= CGSW_GEN_DBG_NUMBERS;
    if( *OptScanPtr == '+' ) {
        ++OptScanPtr;
        CompFlags.debug_info_some = true;
        GenSwitches |= CGSW_GEN_DBG_TYPES | CGSW_GEN_DBG_LOCALS;
    }
    EnsureEndOfSwitch();
}
static void Set_D2( void )
{
    debug_optimization_change = true;
    GenSwitches |= CGSW_GEN_DBG_NUMBERS | CGSW_GEN_DBG_TYPES | CGSW_GEN_DBG_LOCALS;
    if( *OptScanPtr == '~' ) {
        ++OptScanPtr;
        CompFlags.no_debug_type_names = true;
    }
    EnsureEndOfSwitch();
}
static void Set_D3( void )
{
    CompFlags.dump_types_with_names = true;
    Set_D2();
}
static void Set_D9( void )          { CompFlags.use_full_codegen_od = true; }
static void DefineMacro( void )     { OptScanPtr = Define_UserMacro( OptScanPtr ); }

static void SetErrorLimit( void )   { ErrLimit = OptValue; }

#if _INTEL_CPU
static void SetDftCallConv( void )
{
    switch( OptValue ) {
    case 1:
        DftCallConv = &CdeclInfo;
        break;
    case 2:
        DftCallConv = &StdcallInfo;
        break;
    case 3:
        DftCallConv = &FastcallInfo;
        break;
    case 4:
        DftCallConv = &OptlinkInfo;
        break;
    case 5:
        DftCallConv = &PascalInfo;
        break;
    case 6:
        DftCallConv = &SyscallInfo;
        break;
    case 7:
        DftCallConv = &FortranInfo;
        break;
    case 8:
    default:
        DftCallConv = &WatcallInfo;
        break;
    }
}
static void Set_EC( void )          { CompFlags.ec_switch_used = true; }
#endif

static void Set_EE( void )          { CompFlags.ee_switch_used = true; }
static void Set_EF( void )          { CompFlags.ef_switch_used = true; }
static void Set_EN( void )          { CompFlags.emit_names = true; }
static void Set_EI( void )          { CompFlags.make_enums_an_int = true;
                                      CompFlags.original_enum_setting = true;}
static void Set_EM( void )          { CompFlags.make_enums_an_int = false;
                                      CompFlags.original_enum_setting = false;}

#if _INTEL_CPU
static void Set_ET( void )          { TargetSwitches |= CGSW_X86_P5_PROFILING; }
static void Set_ETP( void )         { TargetSwitches |= CGSW_X86_NEW_P5_PROFILING; }
static void Set_ESP( void )         { TargetSwitches |= CGSW_X86_STATEMENT_COUNTING; }
  #if _CPU == 386
static void Set_EZ( void )          { TargetSwitches |= CGSW_X86_EZ_OMF; }
static void Set_OMF( void )         { GenSwitches &= ~(CGSW_GEN_OBJ_ELF | CGSW_GEN_OBJ_COFF); }
  #endif
#endif

#if _RISC_CPU /* || _CPU == 386 */
static void Set_ELF( void )         { GenSwitches = (GenSwitches & ~CGSW_GEN_OBJ_COFF) | CGSW_GEN_OBJ_ELF; }
static void Set_COFF( void )        { GenSwitches = (GenSwitches & ~CGSW_GEN_OBJ_ELF) | CGSW_GEN_OBJ_COFF; }
#endif
#if _RISC_CPU
static void Set_EndianLittle( void ) { GenSwitches &= ~CGSW_GEN_OBJ_ENDIAN_BIG; }
static void Set_EndianBig( void )    { GenSwitches |= CGSW_GEN_OBJ_ENDIAN_BIG; }
#endif

static void Set_EP( void )
{
    CompFlags.ep_switch_used = true;
    ProEpiDataSize = OptValue;
}

static void Set_FH( void )
{
    if( OptParm == OptScanPtr ) {
        PCH_FileName = DEFAULT_PCH_NAME;
    } else {
        PCH_FileName = GetAFileName();
    }
}

static void Set_FHQ( void )
{
    CompFlags.no_pch_warnings = true;
    Set_FH();
}

static void Set_FI( void )
{
    CMemFree( ForceInclude );
    ForceInclude = GetAFileName();
}

static void Set_FIP( void )
{
    CMemFree( ForcePreInclude );
    ForcePreInclude = GetAFileName();
    if( *ForcePreInclude == '\0' ) {
        CMemFree( ForcePreInclude );
        ForcePreInclude = NULL;
    }
}

static void Set_FLD( void )
{
    CompFlags.use_long_double = true;
}

static void SetTrackInc( void )
{
    CompFlags.track_includes = true;
}

static void Set_FO( void )
{
    CMemFree( ObjectFileName );
    ObjectFileName = GetAFileName();
    CompFlags.cpp_output_to_file = true;    /* in case '-p' option */
}

static void Set_FR( void )
{
    CMemFree( ErrorFileName );
    ErrorFileName = GetAFileName();
    if( *ErrorFileName == '\0' ) {
        CMemFree( ErrorFileName );
        ErrorFileName = NULL;
    }
}

static void Set_FT( void )
{
    CompFlags.check_truncated_fnames = true;
}

static void Set_FX( void )
{
    CompFlags.check_truncated_fnames = false;
}

#if _INTEL_CPU
static void SetCodeClass( void )    { CodeClassName = CopyOfParm(); }
static void SetDataSegName( void )
{
    SwData.nd_used = true;
    DataSegName = CopyOfParm();
    ImportNearSegIdInit();
    if( *DataSegName == '\0' ) {
        CMemFree( DataSegName );
        DataSegName = NULL;
    }
}
static void SetTextSegName( void )  { TextSegName = CopyOfParm(); }
static void SetGroup( void )        { GenCodeGroup = CopyOfParm(); }
#endif
static void SetModuleName( void )   { ModuleName = CopyOfParm(); }

static void SetAPILogging( void )   { GenSwitches |= CGSW_GEN_ECHO_API_CALLS; }

#ifdef DEVBUILD
#if _RISC_CPU
static void SetAsmListing( void )   { TargetSwitches |= CGSW_RISC_ASM_OUTPUT; }
static void SetOwlLogging( void )   { TargetSwitches |= CGSW_RISC_OWL_LOGGING; }
#endif
#endif

static void SetInclude( void )
{
    char    *fname;

    fname = GetAFileName();
    AddIncList( fname );
    CMemFree( fname );
}

static void SetReadOnlyDir( void )
{
    char    *dirpath;

    dirpath = GetAFileName();
    SrcFileReadOnlyDir( dirpath );
    CMemFree( dirpath );
}

static void SetCharType( void )
{
    SetSignedChar();
    CompFlags.signed_char = true;
}

#if _INTEL_CPU
static void Set_RE( void )          { CompFlags.rent = true; }
static void Set_RI( void )          { CompFlags.returns_promoted = true; }
static void Set_R( void )           { CompFlags.save_restore_segregs = true; }
static void Set_SG( void )          { CompFlags.sg_switch_used = true; }
static void Set_ST( void )          { CompFlags.st_switch_used = true; }
#endif
#if _CPU == _AXP || _CPU == _MIPS
static void Set_SI( void )          { TargetSwitches |= CGSW_RISC_STACK_INIT; }
#endif
static void Set_S( void )           { TOGGLE( check_stack ) = false; }

static void Set_TP( void )
{
    char    *togname;

    togname = CopyOfParm();
    SetToggleFlag( togname, 1, false );
    CMemFree( togname );
}

static void SetDataThreshHold( void )
{
    if( OptValue > TARGET_INT_MAX ) {
        OptValue = 256;
    }
    DataThreshold = OptValue;
}

static void Set_U( void )
{
    char    *name;

    name = CopyOfParm();
    AddUndefMacro( name );
    CMemFree( name );
}
static void Set_V( void )           { CompFlags.generate_prototypes = true; }

static void Set_WE( void )          { CompFlags.warnings_cause_bad_exit = true; }
static void Set_WO( void )          { CompFlags.using_overlays = true; }
static void Set_WPX( void )         { Check_global_prototype = true; }
static void Set_WX( void )          { WngLevel = WLEVEL_WX; }
static void SetWarningLevel( void ) { WngLevel = OptValue; if( WngLevel > WLEVEL_MAX ) WngLevel = WLEVEL_MAX; }
static void Set_WCD( void )         { WarnEnableDisable( false, OptValue ); }
static void Set_WCE( void )         { WarnEnableDisable( true, OptValue ); }

#if _CPU == 386
static void Set_XGV( void )         { TargetSwitches |= CGSW_X86_INDEXED_GLOBALS; }
#endif

static void Set_XBSA( void )
{
    CompFlags.unaligned_segs = true;
}

#if _CPU == _AXP
static void Set_XD( void )          { TargetSwitches |= CGSW_RISC_EXCEPT_FILTER_USED; }
#endif

static void Set_ZA89( void )
{
    CompVars.cstd = CSTD_C89;
}

static void Set_ZA99( void )
{
    CompVars.cstd = CSTD_C99;
}

static void Set_ZA23( void )
{
    CompVars.cstd = CSTD_C23;
}

static void Set_ZA( void )
{
    CompFlags.extensions_enabled = false;
    CompFlags.non_iso_compliant_names_enabled = false;
    CompFlags.unique_functions = true;
    GenSwitches &= ~CGSW_GEN_I_MATH_INLINE;
}

static void SetStrictANSI( void )
{
    CompFlags.strict_ANSI = true;
    Set_ZA();
}

static void Set_ZAM( void )
{
    CompFlags.non_iso_compliant_names_enabled = false;
}

#if _INTEL_CPU
static void Set_ZC( void )
{
    CompFlags.strings_in_code_segment = true;
    CompFlags.zc_switch_used = true;
    TargetSwitches |= CGSW_X86_CONST_IN_CODE;
}
static void Set_ZDF( void )         { SET_PEGGED( d, false ) }
static void Set_ZDP( void )         { SET_PEGGED( d, true ) }
static void Set_ZDL( void )         { TargetSwitches |= CGSW_X86_LOAD_DS_DIRECTLY; }
static void Set_ZFF( void )         { SET_PEGGED( f, false ) }
static void Set_ZFP( void )         { SET_PEGGED( f, true ) }
static void Set_ZGF( void )         { SET_PEGGED( g, false ) }
static void Set_ZGP( void )         { SET_PEGGED( g, true ) }
#endif
static void Set_ZE( void )
{
    CompFlags.extensions_enabled = true;
    CompFlags.non_iso_compliant_names_enabled = true;
}
static void Set_ZG( void )
{
    CompFlags.generate_prototypes = true;
    CompFlags.dump_prototypes = true;
}

static void Set_ZI( void )          { CompFlags.extra_stats_wanted = true; }

static void Set_ZK( void )          { character_encoding = ENC_ZK; }
static void Set_ZK0( void )         { character_encoding = ENC_ZK0; }
static void Set_ZK1( void )         { character_encoding = ENC_ZK1; }
static void Set_ZK2( void )         { character_encoding = ENC_ZK2; }
static void Set_ZK3( void )         { character_encoding = ENC_ZK3; }
static void Set_ZKL( void )         { character_encoding = ENC_ZKL; }
static void Set_ZKU( void )
{
    character_encoding = ENC_ZKU;
    unicode_CP = OptValue;
}
static void Set_ZK0U( void )        { character_encoding = ENC_ZK0U; }

static void Set_ZL( void )          { CompFlags.emit_library_names = false; }
static void Set_ZLF( void )         { CompFlags.emit_all_default_libs = true; }
static void Set_ZLD( void )         { CompFlags.emit_dependencies = false; }
static void Set_ZLS( void )         { CompFlags.emit_targimp_symbols = false; }
static void Set_ZEV( void )         { CompFlags.unix_ext = true; }
static void Set_ZM( void )
{
    CompFlags.multiple_code_segments = true;
    CompFlags.zm_switch_used = true;
}
static void Set_ZPW( void )         { CompFlags.slack_byte_warning = true; }

#if _INTEL_CPU
static void Set_ZRO( void )
{
    GenSwitches |= CGSW_GEN_FPU_ROUNDING_OMIT;
    GenSwitches &= ~CGSW_GEN_FPU_ROUNDING_INLINE;
}

static void Set_ZRI( void )
{
    GenSwitches |= CGSW_GEN_FPU_ROUNDING_INLINE;
    GenSwitches &= ~CGSW_GEN_FPU_ROUNDING_OMIT;
}
#endif

static void Set_ZQ( void )          { CompFlags.quiet_mode = true; }
static void Set_ZS( void )          { CompFlags.check_syntax = true; }

#if _INTEL_CPU
static void Set_EQ( void )          { CompFlags.eq_switch_used = true; }

static void Set_ZFW( void )
{
    TargetSwitches |= CGSW_X86_GEN_FWAIT_386;
}

static void Set_ZU( void )
{
    CompFlags.zu_switch_used = true;
    TargetSwitches |= CGSW_X86_FLOATING_SS;
}

  #if _CPU == 386
static void Set_ZZ( void )
{
    CompFlags.use_stdcall_at_number = false;
}
  #endif

  #if _CPU == 8086
static void ChkSmartWindows( void )
{
    if( tolower( *(unsigned char *)OptScanPtr ) == 's' ) {
        TargetSwitches |= CGSW_X86_SMART_WINDOWS;
        ++OptScanPtr;
    }
    EnsureEndOfSwitch();
}

static void SetCheapWindows( void )
{
    SetTargetName( "CHEAP_WINDOWS" );
    ChkSmartWindows();
}
  #endif

static void SetWindows( void )
{
    SetTargetName( "WINDOWS" );
  #if _CPU == 8086
    ChkSmartWindows();
  #endif
}
#endif

static void SetGenerateMakeAutoDepend( void )
{
    CompFlags.generate_auto_depend = true;
    CMemFree( DependFileName );
    DependFileName = GetAFileName();
    if( *DependFileName == '\0' ) {
        CMemFree( DependFileName );
        DependFileName = NULL;
    }
}

static void SetAutoDependTarget( void )
{
    /*
     * auto set depend yes...
     */
    CompFlags.generate_auto_depend = true;
    CMemFree( DependTarget );
    DependTarget = GetAFileName();
}

static void SetAutoDependSrcDepend( void )
{
    CompFlags.generate_auto_depend = true;
    CMemFree( SrcDepName );
    SrcDepName = GetAFileName();
}

static void SetAutoDependHeaderPath( void )
{
    CompFlags.generate_auto_depend = true;
    CMemFree( DependHeaderPath );
    DependHeaderPath = GetAFileName();
}

static void SetAutoDependForeSlash( void )
{
    DependForceSlash = '/';
}

static void SetAutoDependBackSlash( void )
{
    DependForceSlash = '\\';
}

static void Set_X( void )           { CompFlags.cpp_ignore_env = true; }
static void Set_XX( void )          { CompFlags.ignore_default_dirs = true; }
static void Set_PIL( void )         { CompFlags.cpp_ignore_line = true; }
static void Set_PL( void )          { CompFlags.cpp_line_wanted = true; }
static void Set_PC( void )
{
    CompFlags.cpp_keep_comments = true;
}
static void Set_PW( void )
{
    if( OptValue != 0
      && OptValue < 20 )
        OptValue = 20;
    if( OptValue > 10000 )
        OptValue = 10000;
    SetPPWidth( OptValue );
}
static void Set_PreProcChar( void ) { PreProcChar = *OptScanPtr++; }

static void Set_OA( void )          { GenSwitches |= CGSW_GEN_RELAX_ALIAS; }
static void Set_OB( void )          { GenSwitches |= CGSW_GEN_BRANCH_PREDICTION; }
static void Set_OD( void )          { GenSwitches |= CGSW_GEN_NO_OPTIMIZATION; }
static void Set_OE( void )
{
    Inline_Threshold = OptValue;
    TOGGLE( inline ) = true;
}

static void Set_OC( void )          { GenSwitches |= CGSW_GEN_NO_CALL_RET_TRANSFORM; }
#if _INTEL_CPU
static void Set_OF( void )
{
    TargetSwitches |= CGSW_X86_NEED_STACK_FRAME;
    if( OptValue != 0 ) {
        WatcallInfo.cclass_target |= FECALL_X86_GENERATE_STACK_FRAME;
    }
}
static void Set_OP( void )          { CompFlags.op_switch_used = true; }    // force floats to memory
#endif
static void Set_OM( void )          { GenSwitches |= CGSW_GEN_I_MATH_INLINE; }
static void Set_OH( void )          { GenSwitches |= CGSW_GEN_SUPER_OPTIMAL; }
static void Set_OK( void )          { GenSwitches |= CGSW_GEN_FLOW_REG_SAVES; }
static void Set_OI( void )          { CompFlags.inline_functions = true; }
static void Set_OL( void )          { GenSwitches |= CGSW_GEN_LOOP_OPTIMIZATION; }
static void Set_OL_plus( void )     { GenSwitches |= CGSW_GEN_LOOP_OPTIMIZATION | CGSW_GEN_LOOP_UNROLLING; }
static void Set_ON( void )          { GenSwitches |= CGSW_GEN_FP_UNSTABLE_OPTIMIZATION; }
static void Set_OO( void )          { GenSwitches &= ~CGSW_GEN_MEMORY_LOW_FAILS; }
static void Set_OR( void )          { GenSwitches |= CGSW_GEN_INS_SCHEDULING; }
static void Set_OS( void )          { GenSwitches &= ~CGSW_GEN_NO_OPTIMIZATION; OptSize = 100; }
static void Set_OT( void )          { GenSwitches &= ~CGSW_GEN_NO_OPTIMIZATION; OptSize = 0; }
static void Set_OU( void )          { CompFlags.unique_functions = true; }
static void Set_OX( void )
{
    TOGGLE( check_stack ) = false;
    GenSwitches &= ~CGSW_GEN_NO_OPTIMIZATION;
    GenSwitches |= CGSW_GEN_LOOP_OPTIMIZATION | CGSW_GEN_INS_SCHEDULING | CGSW_GEN_BRANCH_PREDICTION;
    CompFlags.inline_functions = true;
    OptValue = 20; // Otherwise we effectively disable inlining!
    Set_OE();
    GenSwitches |= CGSW_GEN_I_MATH_INLINE;
}
static void Set_OZ( void )          { GenSwitches |= CGSW_GEN_NULL_DEREF_OK; }
/*
 * '=' indicates optional '='
 * '#' indicates a decimal numeric value
 * '$' indicates identifier
 * '@' indicates filename
 * '*' indicates additional characters will be scanned by option routine
 * if a capital letter appears in the option, then input must match exactly
 * otherwise all input characters are changed to lower case before matching
 */
static struct option const Optimization_Options[] = {
    { "a",      0,              Set_OA },
    { "b",      0,              Set_OB },
    { "d",      0,              Set_OD },
    { "e=#",    20,             Set_OE },
    { "c",      0,              Set_OC },
#if _INTEL_CPU
    { "f+",     1,              Set_OF },
    { "f",      0,              Set_OF },
#endif
    { "m",      0,              Set_OM },
#if _INTEL_CPU
    { "p",      0,              Set_OP },
#endif
    { "h",      0,              Set_OH },
    { "i",      0,              Set_OI },
    { "k",      0,              Set_OK },
    { "l+",     0,              Set_OL_plus },
    { "l",      0,              Set_OL },
    { "n",      0,              Set_ON },
    { "o",      0,              Set_OO },
    { "r",      0,              Set_OR },
    { "s",      0,              Set_OS },
    { "t",      0,              Set_OT },
    { "u",      0,              Set_OU },
    { "x",      0,              Set_OX },
    { "z",      0,              Set_OZ },
    { 0,        0,              0 },
};

static struct option const Preprocess_Options[] = {
    { "c",      0,              Set_PC },
    { "l",      0,              Set_PL },
    { "w=#",    0,              Set_PW },
    { "=",      0,              Set_PreProcChar },
    { "#",      0,              Set_PreProcChar },
    { 0,        0,              0 },
};

static void SetOptimization( void );
static void SetPreprocessOptions( void );
static struct option const CFE_Options[] = {
    { "o*",     0,              SetOptimization },
    { "i=@",    0,              SetInclude },
    { "zq",     0,              Set_ZQ },
    { "q",      0,              Set_ZQ },
#if _INTEL_CPU
  #if _CPU == 8086
    { "0",      SW_CPU0,        SetCPU },
    { "1",      SW_CPU1,        SetCPU },
    { "2",      SW_CPU2,        SetCPU },
    { "3",      SW_CPU3,        SetCPU },
    { "4",      SW_CPU4,        SetCPU },
    { "5",      SW_CPU5,        SetCPU },
    { "6",      SW_CPU6,        SetCPU },
  #else
    { "6r",     SW_CPU6,        SetCPU_xR },
    { "6s",     SW_CPU6,        SetCPU_xS },
    { "6",      SW_CPU6,        SetCPU },
    { "5r",     SW_CPU5,        SetCPU_xR },
    { "5s",     SW_CPU5,        SetCPU_xS },
    { "5",      SW_CPU5,        SetCPU },
    { "4r",     SW_CPU4,        SetCPU_xR },
    { "4s",     SW_CPU4,        SetCPU_xS },
    { "4",      SW_CPU4,        SetCPU },
    { "3r",     SW_CPU3,        SetCPU_xR },
    { "3s",     SW_CPU3,        SetCPU_xS },
    { "3",      SW_CPU3,        SetCPU },
  #endif
#endif
    { "aa",     0,              Set_AA },
    /*
     * more specific commands first ... otherwise the
     * short command sets us up for failure...
     */
    { "adt=@",  0,              SetAutoDependTarget },
    { "adbs",   0,              SetAutoDependBackSlash },
    { "add=@",  0,              SetAutoDependSrcDepend },
    { "adfs",   0,              SetAutoDependForeSlash },
    { "adhp=@", 0,              SetAutoDependHeaderPath },
    { "ad=@",   0,              SetGenerateMakeAutoDepend },
    { "ai",     0,              Set_AI },
    { "aq",     0,              Set_AQ },
#if _CPU == _AXP
    { "as",     0,              Set_AS },
#endif
    { "d0*",    0,              Set_D0 },
    { "d1*",    1,              Set_D1 },
    { "d2*",    2,              Set_D2 },
    { "d3*",    3,              Set_D3 },
    { "d9*",    9,              Set_D9 },
    { "d+*",    0,              SetExtendedDefines },
    { "db",     0,              SetBrowserInfo },
    { "d*",     0,              DefineMacro },
    { "en",     0,              Set_EN },
    { "ep=#",   0,              Set_EP },
    { "ee",     0,              Set_EE },
    { "ef",     0,              Set_EF },
    { "ei",     0,              Set_EI },
    { "em",     0,              Set_EM },
#if _INTEL_CPU
    { "ecc",    1,              SetDftCallConv },
    { "ecd",    2,              SetDftCallConv },
    { "ecf",    3,              SetDftCallConv },
    { "eco",    4,              SetDftCallConv },
    { "ecp",    5,              SetDftCallConv },
    { "ecs",    6,              SetDftCallConv },
    { "ecr",    7,              SetDftCallConv },
    { "ecw",    8,              SetDftCallConv },
    { "ec",     0,              Set_EC },
    { "et",     0,              Set_ET },
    { "eq",     0,              Set_EQ },
    { "etp",    0,              Set_ETP },
    { "esp",    0,              Set_ESP },
#endif
#if _RISC_CPU /* || _CPU == 386 */
    { "eoe",    0,              Set_ELF },
    { "eoc",    0,              Set_COFF },
#endif
#if _RISC_CPU
    { "el",     0,              Set_EndianLittle },
    { "eb",     0,              Set_EndianBig },
#endif
#if _CPU == 386
    { "eoo",    0,              Set_OMF },
    { "ez",     0,              Set_EZ },
#endif
    { "e=#",    0,              SetErrorLimit },
#if _INTEL_CPU
    { "hw",     SW_DF_WATCOM,   Set_DbgFmt },
#endif
    { "hda",    SW_DF_DWARF_A,  Set_DbgFmt },
    { "hdg",    SW_DF_DWARF_G,  Set_DbgFmt },
    { "hd",     SW_DF_DWARF,    Set_DbgFmt },
    { "hc",     SW_DF_CV,       Set_DbgFmt },
#if _INTEL_CPU
    { "g=$",    0,              SetGroup },
#endif
    { "lc",     0,              SetAPILogging },
#ifdef DEVBUILD
#if _RISC_CPU
    { "la",     0,              SetAsmListing },
    { "lo",     0,              SetOwlLogging },
#endif
#endif
#if _INTEL_CPU
    { "ms",     SW_MS,          SetMemoryModel },
    { "mm",     SW_MM,          SetMemoryModel },
    { "mc",     SW_MC,          SetMemoryModel },
    { "ml",     SW_ML,          SetMemoryModel },
  #if _CPU == 8086
    { "mh",     SW_MH,          SetMemoryModel },
  #else
    { "mf",     SW_MF,          SetMemoryModel },
  #endif
    { "nc=$",   0,              SetCodeClass },
    { "nd=$",   0,              SetDataSegName },
#endif
    { "nm=$",   0,              SetModuleName },
#if _INTEL_CPU
    { "nt=$",   0,              SetTextSegName },
#endif
    { "pil",    0,              Set_PIL },
    { "p*",     0,              SetPreprocessOptions },
    { "rod=@",  0,              SetReadOnlyDir },
#if _INTEL_CPU
    { "re",     0,              Set_RE },
    { "ri",     0,              Set_RI },
    { "r",      0,              Set_R },
    { "sg",     0,              Set_SG },
    { "st",     0,              Set_ST },
#endif
#if _CPU == _AXP || _CPU == _MIPS
    { "si",     0,              Set_SI },
#endif
    { "s",      0,              Set_S },
    { "bd",     0,              Set_BD },
    { "bc",     0,              Set_BC },
    { "bg",     0,              Set_BG },
    { "bm",     0,              Set_BM },
#if _CPU != 8086
    { "br",     0,              Set_BR },
#endif
    { "bw",     0,              Set_BW },
    { "bt=$",   0,              Set_BT },
    { "fhq=@",  0,              Set_FHQ },
    { "fh=@",   0,              Set_FH },
    { "fip=@",  0,              Set_FIP },
    { "fi=@",   0,              Set_FI },
    { "fld",    0,              Set_FLD },
    { "fo=@",   0,              Set_FO },
    { "fr=@",   0,              Set_FR },
    { "ft",     0,              Set_FT },
    { "fti",    0,              SetTrackInc },
    { "fx",     0,              Set_FX },
#if _INTEL_CPU
    { "fp2",    SW_FPU0,        SetFPU },
    { "fp3",    SW_FPU3,        SetFPU },
    { "fp5",    SW_FPU5,        SetFPU },
    { "fp6",    SW_FPU6,        SetFPU },
    { "fpr",    0,              Set_FPR },
    { "fpi87",  0,              Set_FPI87 },
    { "fpi",    0,              Set_Emu },
    { "fpc",    0,              Set_FPC },
    { "fpd",    0,              Set_FPD },
#endif
    { "j",      0,              SetCharType },
    { "tp=$",   0,              Set_TP },
    { "u$",     0,              Set_U },
    { "v",      0,              Set_V },
    { "wcd=#",  0,              Set_WCD },
    { "wce=#",  0,              Set_WCE },
    { "we",     0,              Set_WE },
    { "wo",     0,              Set_WO },
    { "wpx",    0,              Set_WPX },
    { "wx",     0,              Set_WX },
    { "w=#",    0,              SetWarningLevel },
    { "x",      0,              Set_X },
#if _CPU == 386
    { "xgv",    0,              Set_XGV },
#endif
    { "xbsa",   0,              Set_XBSA },
#if _CPU == _AXP
    { "xd",     0,              Set_XD },
#endif
    { "xx",     0,              Set_XX },
    { "za89",   0,              Set_ZA89 },
    { "za99",   0,              Set_ZA99 },
    { "za23",   0,              Set_ZA23 },
    { "zam",    0,              Set_ZAM },
    { "zA",     0,              SetStrictANSI },
    { "za",     0,              Set_ZA },
#if _INTEL_CPU
    { "zc",     0,              Set_ZC },
    { "zdf",    0,              Set_ZDF },
    { "zdp",    0,              Set_ZDP },
    { "zdl",    0,              Set_ZDL },
    { "zff",    0,              Set_ZFF },
    { "zfp",    0,              Set_ZFP },
    { "zgf",    0,              Set_ZGF },
    { "zgp",    0,              Set_ZGP },
#endif
    { "ze",     0,              Set_ZE },
#if _INTEL_CPU
    { "zfw",    0,              Set_ZFW },
#endif
    { "zg",     0,              Set_ZG },
    { "zi",     0,              Set_ZI },
    { "zk0u",   0,              Set_ZK0U },
    { "zk0",    0,              Set_ZK0 },
    { "zk1",    0,              Set_ZK1 },
    { "zk2",    0,              Set_ZK2 },
    { "zk3",    0,              Set_ZK3 },
    { "zkl",    0,              Set_ZKL },
    { "zku=#",  0,              Set_ZKU },
    { "zk",     0,              Set_ZK },
    { "zld",    0,              Set_ZLD },
    { "zlf",    0,              Set_ZLF },
    { "zls",    0,              Set_ZLS },
    { "zl",     0,              Set_ZL },
    { "zm",     0,              Set_ZM },
    { "zpw",    0,              Set_ZPW },
    { "zp=#",   1,              Set_ZP },
#if _CPU == _AXP
    { "zps",    0,              SetStructPack },
#endif
#if _INTEL_CPU
    { "zro",    0,              Set_ZRO },
    { "zri",    0,              Set_ZRI },
#endif
    { "zs",     0,              Set_ZS },
    { "zt=#",   256,            SetDataThreshHold },
#if _INTEL_CPU
    { "zu",     0,              Set_ZU },
#endif
    { "zev",    0,              Set_ZEV },
#if _INTEL_CPU
  #if _CPU == 8086
    { "zW*",    0,              SetCheapWindows },
    { "zw*",    0,              SetWindows },
  #else
    { "zw",     0,              SetWindows },
    { "zz",     0,              Set_ZZ },
  #endif
#endif
    { 0,        0,              0 },
};

static const char *ProcessOption( struct option const *op_table, const char *p, const char *option_start )
{
    int         i;
    int         j;
    char        *opt;
    char        c;

    for( i = 0; (opt = op_table[i].option) != NULL; i++ ) {
        c = (char)tolower( *(unsigned char *)p );
        if( c == *opt ) {
            OptValue = op_table[i].value;
            j = 1;
            for( ;; ) {
                ++opt;
                if( *opt == '\0'
                  || *opt == '*' ) {
                    if( *opt == '\0' ) {
                        if( p - option_start == 1 ) {
                            /*
                             * make sure end of option
                             */
                            if( !OptionDelimiter( p[j] ) ) {
                                break;
                            }
                        }
                    }
                    OptScanPtr = p + j;
                    op_table[i].function();
                    return( OptScanPtr );
                }
                if( *opt == '#' ) { /* collect a number */
                    if( p[j] >= '0'
                      && p[j] <= '9' ) {
                        OptValue = 0;
                        for( ;; ) {
                            c = p[j];
                            if( c < '0'
                              || c > '9' )
                                break;
                            OptValue = OptValue * 10 + c - '0';
                            ++j;
                        }
                    }
                } else if( *opt == '$' ) {  /* collect an identifer */
                    OptParm = &p[j];
                    for( ; (c = p[j]) != '\0'; ) {
                        if( c == '-' )
                            break;
                        if( c == ' ' )
                            break;
                        if( c == SwitchChar )
                            break;
                        ++j;
                    }
                } else if( *opt == '@' ) {  /* collect a filename */
                    OptParm = &p[j];
                    c = p[j];
                    if( c == '"' ) { /* "filename" */
                        for( ; (c = p[++j]) != '\0'; ) {
                            if( c == '"' ) {
                                ++j;
                                break;
                            }
                            if( c == '\\' ) {
                                ++j;
                            }
                        }
                    } else {
                        for( ; (c = p[j]) != '\0'; ) {
                            if( c == ' ' )
                                break;
                            if( c == '\t' )
                                break;
#if !defined( __UNIX__ )
                            if( c == SwitchChar )
                                break;
#endif
                            ++j;
                        }
                    }
                } else if( *opt == '=' ) {  /* collect an optional '=' */
                    if( EqualChar( p[j] ) ) {
                        ++j;
                    }
                } else {
                    c = (char)tolower( (unsigned char)p[j] );
                    if( *opt != c ) {
                        if( *opt < 'A'
                          || *opt > 'Z' )
                            break;
                        if( *opt != p[j] ) {
                            break;
                        }
                    }
                    ++j;
                }
            }
        }
    }
    if( op_table == Optimization_Options ) {
        p = BadCmdLine( ERR_INVALID_OPTIMIZATION, p );
    } else {
        p = BadCmdLine( ERR_INVALID_OPTION, option_start );
    }
    return( p );
}

static void ProcessSubOption( struct option const *op_table )
{
    const char  *option_start;

    option_start = OptScanPtr - 2;
    for( ;; ) {
        OptScanPtr = ProcessOption( op_table, OptScanPtr, option_start );
        if( OptionDelimiter( *OptScanPtr ) ) {
            break;
        }
    }
}

static void SetOptimization( void )
{
    ProcessSubOption( Optimization_Options );
}

static void SetPreprocessOptions( void )
{
    CompFlags.cpp_mode = true;
    if( !OptionDelimiter( *OptScanPtr ) ) {
        ProcessSubOption( Preprocess_Options );
    }
}

static const char *CollectEnvOrFileName( const char *str )
{
    char        *env;
    char        ch;

    while( *str == ' '
      || *str == '\t' )
        ++str;
    env = TokenBuf;
    for( ; (ch = *str) != '\0'; ) {
        ++str;
        if( ch == ' ' )
            break;
        if( ch == '\t' )
            break;
#if ! defined( __UNIX__ )
        if( ch == '-' )
            break;
        if( ch == SwitchChar )
            break;
#endif
        *env++ = ch;
    }
    *env = '\0';
    return( str );
}

static char *ReadIndirectFile( void )
{
    char        *env;
    char        *str;
    FILE        *fp;
    size_t      len;
    char        ch;

    env = NULL;
    fp = fopen( TokenBuf, "rb" );
    if( fp != NULL ) {
        fseek( fp, 0, SEEK_END );
        len = (size_t)ftell( fp );
        env = CMemAlloc( len + 1 );
        rewind( fp );
        len = fread( env, 1, len, fp );
        env[len] = '\0';
        fclose( fp );
        /*
         * zip through characters changing \r, \n etc into ' '
         */
        str = env;
        while( (ch = *str) != '\0' ) {
            if( ch == '\r'
              || ch == '\n' ) {
                *str = ' ';
            }
#if !defined( __UNIX__ )
            /*
             * if DOS end of file (^Z) -> mark end of str
             */
            if( ch == 0x1A ) {
                *str = '\0';
                break;
            }
#endif
            ++str;
        }
    }
    return( env );
}

static void ProcOptions( const char *str )
{
    int         level;
    const char  *save[MAX_NESTING];
    char        *buffers[MAX_NESTING];
    const char  *penv;
    char        *ptr;

    if( str != NULL ) {
        level = -1;
        for( ;; ) {
            while( *str == ' '
              || *str == '\t' )
                ++str;
            if( *str == '@' ) {
                str = CollectEnvOrFileName( str + 1 );
                level++;
                if( level < MAX_NESTING ) {
                    save[level] = str;
                    ptr = NULL;
                    penv = FEGetEnv( TokenBuf );
                    if( penv == NULL ) {
                        ptr = ReadIndirectFile();
                        penv = ptr;
                    }
                    buffers[level] = ptr;
                    if( penv != NULL ) {
                        str = penv;
                        continue;
                    }
                }
                level--;
            }
            if( *str == '\0' ) {
                if( level < 0 )
                    break;
                CMemFree( buffers[level] );
                str = save[level];
                level--;
                continue;
            }
            if( *str == '-'
              || *str == SwitchChar ) {
                str = ProcessOption( CFE_Options, str + 1, str );
            } else {  /* collect  file name */
                const char  *beg;
                char        *p;
                char        c;

                beg = str;
                if( *str == '"' ) {
                    ++str;
                    for( ; (c = *str) != '\0';  ) {
                        ++str;
                        if( c == '"' ) {
                            break;
                        }
                        if( c == '\\' ) {
                            ++str;
                        }
                    }
                } else {
                    for( ; (c = *str) != '\0'; ++str ) {
                        if( c == ' '  )
                            break;
                        if( c == '\t'  )
                            break;
#if ! defined( __UNIX__ )
                        if( c == SwitchChar ) {
                            break;
                        }
#endif
                    }
                }
                p = ToStringDup( beg, str - beg );
                StripQuotes( p );
                if( WholeFName != NULL ) {
                    /*
                     * more than one file to compile ?
                     */
                    CBanner();
                    CErr1( ERR_CAN_ONLY_COMPILE_ONE_FILE );
                    CMemFree( WholeFName );
                }
                WholeFName = p;
            }
        }
    }
}

static void InitCPUModInfo( void )
{
    CodeClassName = NULL;
    PCH_FileName = NULL;
    TargetSwitches = 0;
    TargetSystem = TS_OTHER;
    GenSwitches = CGSW_GEN_MEMORY_LOW_FAILS;
#if _INTEL_CPU
    Stack87 = 8;
    TextSegName = "";
    DataSegName = "";
    GenCodeGroup = "";
    CompFlags.register_conventions = true;
#elif _RISC_CPU || _CPU == _SPARC
    TextSegName = "";
    DataSegName = ".data";
    GenCodeGroup = "";
    DataPtrSize = TARGET_POINTER;
    CodePtrSize = TARGET_POINTER;
#else
    #error InitCPUModInfo not configured for system
#endif
}

static void Define_Memory_Model( void )
{
#if _INTEL_CPU
    char        lib_model;
#endif

    DataPtrSize = TARGET_POINTER;
    CodePtrSize = TARGET_POINTER;
#if _INTEL_CPU
    switch( SwData.mem ) {
    case SW_MF:
        lib_model = 's';
        TargetSwitches &= ~CGSW_X86_CONST_IN_CODE;
        break;
    case SW_MS:
        lib_model = 's';
        CompFlags.strings_in_code_segment = false;
        TargetSwitches &= ~CGSW_X86_CONST_IN_CODE;
        break;
    case SW_MM:
        lib_model = 'm';
        WatcallInfo.cclass_target |= FECALL_X86_FAR_CALL;
        CompFlags.strings_in_code_segment = false;
        TargetSwitches &= ~CGSW_X86_CONST_IN_CODE;
        CodePtrSize = TARGET_FAR_POINTER;
        break;
    case SW_MC:
        lib_model = 'c';
        DataPtrSize = TARGET_FAR_POINTER;
        break;
    case SW_ML:
        lib_model = 'l';
        WatcallInfo.cclass_target |= FECALL_X86_FAR_CALL;
        CodePtrSize = TARGET_FAR_POINTER;
        DataPtrSize = TARGET_FAR_POINTER;
        break;
    case SW_MH:
        lib_model = 'h';
        WatcallInfo.cclass_target |= FECALL_X86_FAR_CALL;
        CodePtrSize = TARGET_FAR_POINTER;
        DataPtrSize = TARGET_FAR_POINTER;
        break;
    default:
        lib_model = '?';
        break;
    }
#endif
#if _CPU == 8086
    strcpy( CLIB_Name, "1clib?" );
    if( CompFlags.bm_switch_used ) {
        strcpy( CLIB_Name, "1clibmt?" );
    }
    if( CompFlags.bd_switch_used ) {
        if( TargetSystem == TS_WINDOWS
          || TargetSystem == TS_CHEAP_WINDOWS ) {
            strcpy( CLIB_Name, "1clib?" );
        } else {
            strcpy( CLIB_Name, "1clibdl?" );
        }
    }
    if( GET_FPU_EMU( ProcRevision ) ) {
        strcpy( MATHLIB_Name, "7math87?" );
        EmuLib_Name = "8emu87";
    } else if( GET_FPU_LEVEL( ProcRevision ) == FPU_NONE ) {
        strcpy( MATHLIB_Name, "5math?" );
        EmuLib_Name = NULL;
    } else {
        strcpy( MATHLIB_Name, "7math87?" );
        EmuLib_Name = "8noemu87";
    }
#elif _CPU == 386
    lib_model = 'r';
    if( !CompFlags.register_conventions )
        lib_model = 's';
    if( CompFlags.br_switch_used ) {
        strcpy( CLIB_Name, "1clb?dll" );
    } else {
        strcpy( CLIB_Name, "1clib3?" );     /* There is only 1 CLIB now! */
    }
    if( GET_FPU_EMU( ProcRevision ) ) {
        if( CompFlags.br_switch_used ) {
            strcpy( MATHLIB_Name, "7mt7?dll" );
        } else {
            strcpy( MATHLIB_Name, "7math387?" );
        }
        EmuLib_Name = "8emu387";
    } else if( GET_FPU_LEVEL( ProcRevision ) == FPU_NONE ) {
        if( CompFlags.br_switch_used ) {
            strcpy( MATHLIB_Name, "5mth?dll" );
        } else {
            strcpy( MATHLIB_Name, "5math3?" );
        }
        EmuLib_Name = NULL;
    } else {
        if( CompFlags.br_switch_used ) {
            strcpy( MATHLIB_Name, "7mt7?dll" );
        } else {
            strcpy( MATHLIB_Name, "7math387?" );
        }
        EmuLib_Name = "8noemu387";
    }
#elif _RISC_CPU || _CPU == _SPARC
    if( CompFlags.br_switch_used ) {
        strcpy( CLIB_Name, "1clbdll" );
        strcpy( MATHLIB_Name, "7mthdll" );
    } else {
        strcpy( CLIB_Name, "1clib" );
        strcpy( MATHLIB_Name, "7math" );
    }
    EmuLib_Name = NULL;
#else
    #error Define_Memory_Model not configured
#endif
#if _INTEL_CPU
    *strchr( CLIB_Name, '?' ) = lib_model;
    *strchr( MATHLIB_Name, '?' ) = lib_model;
#endif
}

static void SetDebug( void )
{
    if( debug_optimization_change ) {
        GenSwitches |= CGSW_GEN_NO_OPTIMIZATION;
        CompFlags.inline_functions = false;
    }
}

void GenCOptions( char **cmdline )
{
    memset( &SwData,0, sizeof( SwData ) ); /* re-useable */
    /*
     * Add precision warning but disabled by default
     */
    WarnEnableDisable( false, ERR_LOSE_PRECISION );
    /*
     * Warning about non-prototype declarations is disabled by default
     * because Windows and OS/2 API headers use it
     */
    WarnEnableDisable( false, ERR_OBSOLETE_FUNC_DECL );
    /*
     * Warning about pointer truncation during cast is disabled by
     * default because it would cause too many build breaks right now
     * by correctly diagnosing broken code.
     */
    WarnEnableDisable( false, ERR_CAST_POINTER_TRUNCATION );
    InitModInfo();
    InitCPUModInfo();
#if _CPU == 8086
    ProcOptions( FEGetEnv( "WCC" ) );
#elif _CPU == 386
    ProcOptions( FEGetEnv( "WCC386" ) );
#elif _CPU == _AXP
    ProcOptions( FEGetEnv( "WCCAXP" ) );
#elif _CPU == _PPC
    ProcOptions( FEGetEnv( "WCCPPC" ) );
#elif _CPU == _MIPS
    ProcOptions( FEGetEnv( "WCCMPS" ) );
#elif _CPU == _SPARC
    ProcOptions( FEGetEnv( "WCCSPC" ) );
#else
    #error Compiler environment variable not configured
#endif
    for( ;*cmdline != NULL; ++cmdline ) {
        ProcOptions( *cmdline );
    }
    if( CompFlags.cpp_mode ) {
        CompFlags.cpp_output = true;
        CompFlags.quiet_mode = true;
    }
    /*
     * print banner if -zq not specified
     */
    CBanner();
    GblPackAmount = PackAmount;
    SetDebug();
    SetTargetSystem();
    SetFinalTargetSystem();
    SetGenSwitches();
    SetCharacterEncoding();
    Define_Memory_Model();
#if _INTEL_CPU
    if( GET_CPU( ProcRevision ) < CPU_386 ) {
        /*
         * issue warning message if /zf[f|p] or /zg[f|p] spec'd?
         */
        TargetSwitches &= ~(CGSW_X86_FLOATING_FS | CGSW_X86_FLOATING_GS);
    }
    if( !CompFlags.save_restore_segregs ) {
        if( TargetSwitches & CGSW_X86_FLOATING_DS ) {
            HW_CTurnOff( WatcallInfo.save, HW_DS );
        }
        if( TargetSwitches & CGSW_X86_FLOATING_ES ) {
            HW_CTurnOff( WatcallInfo.save, HW_ES );
        }
        if( TargetSwitches & CGSW_X86_FLOATING_FS ) {
            HW_CTurnOff( WatcallInfo.save, HW_FS );
        }
        if( TargetSwitches & CGSW_X86_FLOATING_GS ) {
            HW_CTurnOff( WatcallInfo.save, HW_GS );
        }
    }
  #if _CPU == 386
    if( !CompFlags.register_conventions )
        SetAuxStackConventions();
  #endif
#endif
    MacroDefs();
    MiscMacroDefs();
}
