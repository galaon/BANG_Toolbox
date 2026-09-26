#include "AEConfig.h"
#include "AE_EffectVers.h"

#ifndef AE_OS_WIN
	#include <AE_General.r>
#endif

resource 'PiPL' (16000) {
	{
		Kind { AEEffect },
		Name { "BANG Stroke" },
		Category { "BANG" },
#ifdef AE_OS_WIN
    #if defined(AE_PROC_INTELx64)
		CodeWin64X86 {"EffectMain"},
    #elif defined(AE_PROC_ARM64)
		CodeWinARM64 {"EffectMain"},
    #endif
#elif defined(AE_OS_MAC)
		CodeMacIntel64 {"EffectMain"},
		CodeMacARM64 {"EffectMain"},
#endif
		AE_PiPL_Version { 2, 0 },
		AE_Effect_Spec_Version { PF_PLUG_IN_VERSION, PF_PLUG_IN_SUBVERS },
		AE_Effect_Version { 720897 /* 1.6.0 build 1 */ },
		AE_Effect_Info_Flags { 0 },
		/* PF_OutFlag_DEEP_COLOR_AWARE | PF_OutFlag_I_EXPAND_BUFFER | PF_OutFlag_SEND_UPDATE_PARAMS_UI */
		AE_Effect_Global_OutFlags { 0x06000200 },
		/* PF_OutFlag2_SUPPORTS_SMART_RENDER | FLOAT_COLOR_AWARE | SUPPORTS_THREADED_RENDERING | REVEALS_ZERO_ALPHA | PARAM_GROUP_START_COLLAPSED_FLAG */
		AE_Effect_Global_OutFlags_2 { 0x08001488 },
		AE_Effect_Match_Name { "BANG Stroke" },
		AE_Reserved_Info { 8 },
		AE_Effect_Support_URL { "https://github.com/galaon/BANG_Toolbox" }
	}
};
