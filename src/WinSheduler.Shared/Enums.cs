namespace WinSheduler.Shared;

public enum TimeWindowType { ExactTimes, Interval }
public enum OnErrorAction { SkipNext, Retry, Fail }
public enum OnOverlapAction { WaitComplete, WaitNext, StartAnother }
public enum RunStatus { Running, Completed, Failed, Killed, Timeout }
public enum WindowStyle { Normal, Hidden, Minimized, Maximized }
