enum struct SynchroActionType: uint8_t
{
	UPDATE_PANEL,
	SAVE_SCREEN,
	RESTORE_SCREEN,
	SHOW_PROGRESS,
	FUNCTION
};

struct SynchroAction
{
	SynchroActionType type;
	bool heap = false;

	SynchroAction(SynchroActionType _type) : type(_type) {};
	virtual ~SynchroAction() = default;
};

template <typename T>
struct SynchroDataAction : SynchroAction
{
	T arg{};

	SynchroDataAction(SynchroActionType _type, T _arg = {}): SynchroAction(_type), arg(_arg) {};
};

struct SynchroFunctionAction : SynchroDataAction<void*>
{
	std::function<void(void*)> func;

	SynchroFunctionAction(std::function<void(void*)> _func): SynchroDataAction<void*>(SynchroActionType::FUNCTION), func(_func) {};
};
