@ Hyrule Town Nayru alone
SCRIPT_START script_NayruAlone
	BeginBlock
	Call Nayru_MakeInteractable
	SetAnimationState 0x0004
	DoPostScriptAction 0x0001
	DoPostScriptAction 0x000a
	EndBlock
script_08012216:
	CheckEntityInteractType
	JumpIfNot script_08012216
	SetPlayerIdle
	FacePlayer
	DoPostScriptAction 0x0000
	CheckLocalFlag MHOUSE_NAYRU_TALK
	JumpIf script_080120EA
	MessageNoOverlap TEXT_HAPPY_HEARTH, 0x25
	WaitUntilTextboxCloses
	SetLocalFlag MHOUSE_NAYRU_TALK
	Jump script_0801212A  @ PC-port QoL: route left-behind Nayru into her existing MovedIn charm-offer path

