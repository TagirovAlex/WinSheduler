namespace WinSheduler.Shared;

public enum TimeWindowType { ExactTimes, Interval }
public enum OnErrorAction { SkipNext, Retry, Fail }
public enum OnOverlapAction { WaitComplete, WaitNext, StartAnother }
public enum RunStatus { Running, Completed, Failed, Killed, Timeout, Aborted }
public enum WindowStyle { Normal, Hidden, Minimized, Maximized }

/// <summary>Режим сессии задачи. <see cref="ActiveUser"/> зарезервирован и в v1 отклоняется.</summary>
public enum SessionMode { Headless, ActiveUser }

/// <summary>Тип триггера расписания (v2).</summary>
public enum TriggerKind { Once, Daily, Weekly, Monthly, Interval }

/// <summary>Источник запуска задачи.</summary>
public enum TriggeredBy { Schedule, Manual }
