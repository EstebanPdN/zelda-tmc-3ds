@ Hyrule Town Din alone
SCRIPT_START script_DinAlone
	BeginBlock
	Call Din_MakeInteractable
	SetAnimationState 0x0004
	DoPostScriptAction 0x0001
	DoPostScriptAction 0x000a
	EndBlock
script_080121DE:
	CheckEntityInteractType
	JumpIfNot script_080121DE
	SetPlayerIdle
	FacePlayer
	DoPostScriptAction 0x0000
	CheckLocalFlag MHOUSE_DIN_TALK
	JumpIf script_0801205E
	MessageNoOverlap TEXT_HAPPY_HEARTH, 0x27
	WaitUntilTextboxCloses
	SetLocalFlag MHOUSE_DIN_TALK
	Jump script_0801209E  @ PC-port QoL: route left-behind Din into her existing MovedIn charm-offer path

