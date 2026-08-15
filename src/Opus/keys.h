/* This file defines the keyboard interface used by Opus. */


/* Key Types (used in keymap structure) */
#define ktNil           -1
#define ktKeyMacro      0       /* keyboard macro (not used) */
#define ktIgnore        1       /* just ignore the key */
#define ktBeep          2       /* beep and ignore the key */
#define ktInsert        3       /* kc is meant for insert loop */

#define ktMacro         6       /* Opel macro */
#define ktFunc          7       /* CS function */
/* ktFunc rows below use a designated initializer (.pfn =) so the function
   address lands in the KME union's PFN member. A plain positional
   initializer would target the union's first member, `int w`, truncating
   the 64-bit function pointer on x64. */


/* kcModal is a special key code used to simulate a keypress ending a mode */
#define kcModal 0x0fff


/* Keyboard Character Codes */

/*

A Keyboard Character Code (KC) is a 16-bit code that describes a key
and its Shift, Control, and Alt states.

*/

#define kcNil                   0xFFFF   /* No key, ignore it */

#define kcMinVisi 0x20
#define kcMaxVisi 0x7f
#define FCmdKc(kc) ((uns)((kc) - kcMinVisi) >= kcMaxVisi - kcMinVisi)


/* Local names for Windows key codes */

#define kcBackSpace     (VK_BACK)
#define kcDelNext       (VK_DELETE)
#define kcTab           (VK_TAB)
#define kcReturn        (VK_RETURN)
#define kcEscape        (VK_ESCAPE)

#define kcSpace         (VK_SPACE)
#define kcInsert        (VK_INSERT)
#define kcDelete        (VK_DELETE)
#define kcHelp          (VK_HELP)
#define kcF1            (VK_F1)
#define kcF16           (VK_F16)


#define kcPageBreak             KcCtrl(kcReturn)

#define kcColumnBreak           KcCtrl(KcShift(kcReturn))
#define kcNonReqHyphen          KcCtrl(kcMinus) /* BEWARE: non-standard VK */
#define kcNonBreakHyphen        KcShift(kcNonReqHyphen)
#define kcNonBreakSpace         KcCtrl(KcShift(' '))
#define kcNewLine               KcShift(kcReturn)

/* Cursor movement keys */

#define kcUp            (VK_UP)
#define kcDown          (VK_DOWN)
#define kcLeft          (VK_LEFT)
#define kcRight         (VK_RIGHT)
#define kcBeginLine     (VK_HOME)
#define kcEndLine       (VK_END)
#define kcPageUp        (VK_PRIOR)
#define kcPageDown      (VK_NEXT)
#define kcClear         (VK_CLEAR)

#define kcPrevPara	KcCtrl(kcUp)
#define kcNextPara	KcCtrl(kcDown)
#define kcWordLeft	KcCtrl(kcLeft)
#define kcWordRight	KcCtrl(kcRight)
#define kcWordLeftAlt	KcAlt(kcLeft)
#define kcWordRightAlt	KcAlt(kcRight)
#define kcTopScreen	KcCtrl(kcPageUp)
#define kcEndScreen	KcCtrl(kcPageDown)
#define kcTopDoc	KcCtrl(kcBeginLine)
#define kcEndDoc	KcCtrl(kcEndLine)


#define kcNPPlus	(VK_ADD)
#define kcNPMult	(VK_MULTIPLY)
#define kcNPMinus	(VK_SUBTRACT)
#define kcNPDiv		(VK_DIVIDE)

#ifdef DEBUG
	/* used for dumping windows to clipboard */
#define kcPrintScr     KcCtrl(VK_MULTIPLY)
	/* dumps user state to comm and title bar */
#define kcSysState     KcCtrl(KcShift(VK_ADD))
	/* does a stack trace */
#define kcStackTrace   KcCtrl(KcShift(VK_SUBTRACT))
	/* causes immediate exit from windows */
#define kcExitWin1     KcCtrl(KcShift(VK_F12))
#define kcExitWin2     KcAlt(KcCtrl(KcShift(VK_F2)))
#endif   /* DEBUG */


/* These are the special keys that we use that do not have definate VK codes.
	The vkFoo values are set up during initialization by calling the keyboard
	driver VkKeyScan() function. */
extern int vkPlus, vkMinus, vkStar;
#define kcPlus		(vkPlus)
#define kcMinus		(vkMinus)
#define kcStar		(vkStar)

#ifdef INTL
extern int vkUnderline, vkEquals, vkQuestionmark;
#define kcUnderline	(vkUnderline)
#define kcEquals	(vkEquals)
#define kcQuestionmark	(vkQuestionmark)

#define vkPlusDef	KcShift(0xbb)
#define vkMinusDef	0xbd
#define vkStarDef	KcShift('8')
#define vkUnderlineDef	KcShift(0xbd)
#define vkEqualsDef	0xbb
#define vkQuestionmarkDef KcShift('/')
#endif /* INTL */

#define kcSubscript	KcCtrl(kcPlus)
#define kcSuperscript	KcShift(KcCtrl(kcPlus))
#define kcShowAll	KcCtrl(KcShift(kcStar))

/* Macros to set key code modifier bits */

#define KcCtrl(kc)      ((kc) | 0x100)
#define KcShift(kc)     ((kc) | 0x200)
#define KcAlt(kc)       ((kc) | 0x400)


/* Test key code modifier bits */

#define FCtrlKc(kc)     ((kc) & 0x100)
#define FShiftKc(kc)    ((kc) & 0x200)
#define FAltKc(kc)      ((kc) & 0x400)


/* Remove key code modifier bits */

#define KcStrip(kc)     ((kc) & 0xff)


/* Key Board State Masks */

#define wKbsShiftMask   0x0200
#define wKbsOptionMask  0x0400
#define wKbsControlMask 0x0100

#define wKbsCapsLckMask 0x0800
#define wKbsNumLckMask  0x1000
#define wKbsExtendMask  0x2000

#define wKbsOptionCLMask (wKbsOptionMask | wKbsCapsLckMask)
#define wKbsShiftOptionCLMask (wKbsShiftMask | wKbsOptionCLMask)


/* Fake variables describing the state of modifier keys */

extern int vgrpfKeyBoardState;
#define vfShiftKey (vgrpfKeyBoardState & wKbsShiftMask)
#define vfOptionKey (vgrpfKeyBoardState & wKbsOptionMask)
#define vfControlKey (vgrpfKeyBoardState & wKbsControlMask)


/* Only used in the following kc defs'. */
#define kcPeriod        0xBE
#define kcComma         0xBC
#define kcSlash         0xBF

#define StripCtrl(_kc) (0x00FF & (_kc))

/* If you change the following key assignment, you must change
	tables in outline.c and cmd.c */
#define kcExpand1      (VK_ADD)
#define kcExpand2      (KcAlt(KcShift(kcPlus)))
#define kcExpand3      (KcAlt(KcShift(VK_ADD)))
#define kcCollapse1    (VK_SUBTRACT)
#define kcCollapse2    (KcAlt(KcShift(kcMinus)))
#define kcCollapse3    (KcAlt(KcShift(VK_SUBTRACT)))

#define kcLevel1       (KcAlt(KcShift('1')))
#define kcLevel2       (KcAlt(KcShift('2')))
#define kcLevel3       (KcAlt(KcShift('3')))
#define kcLevel4       (KcAlt(KcShift('4')))
#define kcLevel5       (KcAlt(KcShift('5')))
#define kcLevel6       (KcAlt(KcShift('6')))
#define kcLevel7       (KcAlt(KcShift('7')))
#define kcLevel8       (KcAlt(KcShift('8')))
#define kcLevel9       (KcAlt(KcShift('9')))

#define kcExpandAll1   (VK_MULTIPLY)
#define kcExpandAll2   (KcAlt(KcShift('A')))
#define kcMoveUp       (KcAlt(KcShift(kcUp)))
#define kcMoveDown     (KcAlt(KcShift(kcDown)))
#define kcConvToBody   (KcAlt(KcShift(VK_NUMPAD5)))

#define kcToggleEllip  (KcAlt(KcShift('F')))


/* for the Macro Edit Icon Bar in edmacro.c */

#define kcTraceMacro		KcShift(KcAlt('E'))
#define kcAnimateMacro		KcShift(KcAlt('R'))
#define kcContinueMacro 	KcShift(KcAlt('S'))
#define kcContinueMacro2	KcShift(KcAlt('O'))
#define kcStepMacro		KcShift(KcAlt('U'))
#define kcShowVars		KcShift(KcAlt('V'))


#ifdef PREVIEWC /* from preview.c */

#define rgkmePrvwDef	\
	{ kcTab,	ktFunc,   .pfn = PrvwTab },			\
	{ kcReturn,	ktFunc,   .pfn = PrvwReturn }, 		\
	{ kcEscape,	ktMacro,  bcmPrintPreview },		\
	{ kcPageUp,	ktFunc,   .pfn = PrvwPageUp }, 		\
	{ kcPageDown,	ktFunc,   .pfn = PrvwPageDown },		\
	{ 'A',		ktMacro,  bcmPrvwPages },		\
	{ 'B',		ktMacro,  bcmPrvwBound },		\
	{ 'C',		ktMacro,  bcmPrintPreview },		\
	{ 'P',		ktMacro,  bcmPrint },			\
	{ 'V',		ktMacro,  bcmPageView },		\
	{ VK_F1,	ktFunc,   .pfn = PrvwF1 },			\
	{ VK_F10,	ktMacro,  bcmMenuMode },		\
	{ KcShift(kcTab),      ktFunc,	 .pfn = PrvwTab },		\
	{ KcShift(VK_F1),      ktFunc,	 .pfn = PrvwShiftF1 }, 	\
	{ KcAlt(KcShift('A')), ktMacro, bcmPrvwPages }, 	\
	{ KcAlt(KcShift('B')), ktMacro, bcmPrvwBound }, 	\
	{ KcAlt(KcShift('C')), ktMacro, bcmPrintPreview },	\
	{ KcAlt(KcShift('P')), ktMacro, bcmPrint },		\
	{ KcAlt(KcShift('V')), ktMacro, bcmPageView },

#endif /* PREVIEWC */


#ifdef ICONBAR3C /* from iconbar3.c */

#define rgKmeOutlineDef \
	{ kcExpandAll1,  ktMacro, bcmExpandAll },	\
	{ kcExpand1,	 ktMacro, bcmExpand },		\
	{ kcCollapse1,	 ktMacro, bcmCollapse },	\
	{ kcLevel1,	 ktMacro, bcmShowToLevel1 },	\
	{ kcLevel2,	 ktMacro, bcmShowToLevel2 },	\
	{ kcLevel3,	 ktMacro, bcmShowToLevel3 },	\
	{ kcLevel4,	 ktMacro, bcmShowToLevel4 },	\
	{ kcLevel5,	 ktMacro, bcmShowToLevel5 },	\
	{ kcLevel6,	 ktMacro, bcmShowToLevel6 },	\
	{ kcLevel7,	 ktMacro, bcmShowToLevel7 },	\
	{ kcLevel8,	 ktMacro, bcmShowToLevel8 },	\
	{ kcLevel9,	 ktMacro, bcmShowToLevel9 },	\
	{ (KcAlt(KcShift('A'))),	ktMacro, bcmExpandAll },	\
	{ (KcAlt(KcShift('F'))),	ktMacro, bcmToggleEllip },	\
	{ kcExpand3,	 ktMacro, bcmExpand },		\
	{ kcCollapse3,	 ktMacro, bcmCollapse },


#define rgKmeOutlineIBDef \
	{ kcTab,		    ktFunc, .pfn = IBTab },		\
	{ kcReturn,		    ktFunc, .pfn = IBReturn }, 	\
	{ kcEscape,		    ktFunc, .pfn = IBCancel }, 	\
	{ kcSpace,		    ktFunc, .pfn = IBReturn	},	\
	{ kcLeft,		    ktFunc, .pfn = IBMoveLeft },	\
	{ StripCtrl(kcMoveUp),	    ktFunc, .pfn = IBMoveUp }, 	\
	{ kcRight,		    ktFunc, .pfn = IBMoveRight },	\
	{ StripCtrl(kcMoveDown),    ktFunc, .pfn = IBMoveDown },	\
	{ StripCtrl(kcLevel1),	    ktFunc, .pfn = IBLevel1 }, 	\
	{ StripCtrl(kcLevel2),	    ktFunc, .pfn = IBLevel2 }, 	\
	{ StripCtrl(kcLevel3),	    ktFunc, .pfn = IBLevel3 }, 	\
	{ StripCtrl(kcLevel4),	    ktFunc, .pfn = IBLevel4 }, 	\
	{ StripCtrl(kcLevel5),	    ktFunc, .pfn = IBLevel5 }, 	\
	{ StripCtrl(kcLevel6),	    ktFunc, .pfn = IBLevel6 }, 	\
	{ StripCtrl(kcLevel7),	    ktFunc, .pfn = IBLevel7 }, 	\
	{ StripCtrl(kcLevel8),	    ktFunc, .pfn = IBLevel8 }, 	\
	{ StripCtrl(kcLevel9),	    ktFunc, .pfn = IBLevel9 }, 	\
	{ 'A',			    ktFunc, .pfn = IBExpandAll },	\
	{ StripCtrl(kcConvToBody),  ktFunc, .pfn = IBConvertToBody },	\
	{ StripCtrl(kcExpandAll1),  ktFunc, .pfn = IBExpandAll },	\
	{ StripCtrl(kcExpand1),     ktFunc, .pfn = IBExpand }, 	\
	{ StripCtrl(kcCollapse1),   ktFunc, .pfn = IBCollapse },	\
	{ VK_F1,		    ktFunc, .pfn = IBGetHelpOutline },


#define rgKmeHdrIBDef \
	{ kcTab,	ktFunc, .pfn = IBTab },		\
	{ kcReturn,	ktFunc, .pfn = IBReturn    },		\
	{ kcEscape,	ktFunc, .pfn = IBCancel    },		\
	{ kcSpace,	ktFunc, .pfn = IBReturn    },		\
	{ kcLeft,	ktFunc, .pfn = IBMoveLeft  },		\
	{ kcUp, 	ktFunc, .pfn = IBMoveLeft  },		\
	{ kcRight,	ktFunc, .pfn = IBMoveRight },		\
	{ kcDown,	ktFunc, .pfn = IBMoveRight },		\
	{ 'C',		ktFunc, .pfn = IBHdrRetToDoc },	\
	{ 'D',		ktFunc, .pfn = IBHdrDate },		\
	{ 'L',		ktFunc, .pfn = IBHdrLinkPrev },	\
	{ 'P',		ktFunc, .pfn = IBHdrPage },		\
	{ 'R',		ktFunc, .pfn = IBHdrLinkPrev },	\
	{ 'T',		ktFunc, .pfn = IBHdrTime },		\
	{ VK_F1,	ktFunc, .pfn = IBGetHelpHdr },


#define rgKmeDbgIBDef \
	{ kcTab,	ktFunc, .pfn = IBTab },		\
	{ kcReturn,	ktFunc, .pfn = IBReturn },		\
	{ kcEscape,	ktFunc, .pfn = IBCancel },		\
	{ kcSpace,	ktFunc, .pfn = IBReturn    },		\
	{ kcLeft,	ktFunc, .pfn = IBMoveLeft },		\
	{ kcUp, 	ktFunc, .pfn = IBMoveLeft },		\
	{ kcRight,	ktFunc, .pfn = IBMoveRight },		\
	{ kcDown,	ktFunc, .pfn = IBMoveRight },		\
	{ 'E',		ktFunc, .pfn = IBTraceMacro }, 	\
	{ 'R',		ktFunc, .pfn = IBAnimateMacro },	\
	{ 'S',		ktFunc, .pfn = IBContinueMacro },	\
	{ 'U',		ktFunc, .pfn = IBStepMacro },		\
	{ 'V',		ktFunc, .pfn = IBShowVars },		\
	{ VK_F1,	ktFunc, .pfn = IBGetHelpMacro },


#define rgKmePreviewIBDef \
	{ kcTab,	ktFunc, .pfn = IBTab	    },		\
	{ kcReturn,	ktFunc, .pfn = IBReturn    },		\
	{ kcEscape,	ktFunc, .pfn = IBCancel    },		\
	{ kcSpace,	ktFunc, .pfn = IBReturn    },		\
	{ kcLeft,	ktFunc, .pfn = IBMoveLeft  },		\
	{ kcUp, 	ktFunc, .pfn = IBMoveLeft  },		\
	{ kcRight,	ktFunc, .pfn = IBMoveRight },		\
	{ kcDown,	ktFunc, .pfn = IBMoveRight },		\
	{ 'A',		ktFunc, .pfn = IBPrvwPages },		\
	{ 'B',		ktFunc, .pfn = IBPrvwBound },		\
	{ 'C',		ktFunc, .pfn = IBPrvwClose },		\
	{ 'P',		ktFunc, .pfn = IBPrvwPrint },		\
	{ 'V',		ktFunc, .pfn = IBPageView  },		\
	{ VK_F1,	ktFunc, .pfn = IBGetHelpPreview },


#endif /* ICONBAR3C */


#ifdef RULRIBC /* from rulrib.c */

#define rgKmeRulerDef \
	{ kcTab,	    ktFunc, .pfn = RETab	},	\
	{ kcReturn,	    ktFunc, .pfn = REReturn	},	\
	{ kcEscape,	    ktFunc, .pfn = REEscape	},	\
	{ VK_END,	    ktFunc, .pfn = REEnd	},	\
	{ VK_HOME,	    ktFunc, .pfn = REHome	},	\
	{ kcLeft,	    ktFunc, .pfn = RELeft	},	\
	{ kcRight,	    ktFunc, .pfn = RERight	},	\
	{ kcInsert,	    ktFunc, .pfn = REInsert	},	\
	{ kcDelete,	    ktFunc, .pfn = REDelete	},	\
	{ '1',		    ktFunc, .pfn = RETabLeft	},	\
	{ '2',		    ktFunc, .pfn = RETabCenter },	\
	{ '3',		    ktFunc, .pfn = RETabRight	},	\
	{ '4',		    ktFunc, .pfn = RETabDecimal },	\
	{ 'F',		    ktFunc, .pfn = REIndLeft1	},	\
	{ 'L',		    ktFunc, .pfn = REIndLeft	},	\
	{ 'R',		    ktFunc, .pfn = REIndRight	},	\
	{ VK_F1,	    ktFunc, .pfn = REGetHelp	},


#endif /* RULRIBC */
