enum struct SynchroEventType: uint8_t
{
	UPDATE_PANEL,
	SAVE_SCREEN,
	RESTORE_SCREEN,
	SHOW_PROGRESS,
	FUNCTION
};

struct SynchroEvent
{
	SynchroEventType type;
	bool heap = false;

	SynchroEvent(SynchroEventType _type) : type(_type) {};
	virtual ~SynchroEvent() = default;
};

template <typename T>
struct SynchroDataEvent : SynchroEvent
{
	T arg{};

	SynchroDataEvent(SynchroEventType _type, T _arg = {}): SynchroEvent(_type), arg(_arg) {};
};

struct SynchroFunctionEvent : SynchroDataEvent<void*>
{
	std::function<void(void*)> func;

	SynchroFunctionEvent(std::function<void(void*)> _func): SynchroDataEvent<void*>(SynchroEventType::FUNCTION), func(_func) {};
};
