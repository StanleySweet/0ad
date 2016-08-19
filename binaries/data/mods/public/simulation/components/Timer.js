function Timer() {}

Timer.prototype.Schema =
	"<a:component type='system'/><empty/>";

Timer.prototype.Init = function()
{
	this.id = 0;
	this.time = 0;
	this.timers = new Map();
	this.turnLength = 0;
};

/**
 * Returns time since the start of the game, in integer milliseconds.
 */
Timer.prototype.GetTime = function()
{
	return this.time;
};

Timer.prototype.GetLatestTurnLength = function()
{
	return this.turnLength;
};

/**
 * Create a new timer, which will call the 'funcname' method with arguments (data, lateness)
 * on the 'iid' component of the 'ent' entity, after at least 'time' milliseconds.
 * 'lateness' is how late the timer is executed after the specified time (in milliseconds).
 * Returns a non-zero id that can be passed to CancelTimer.
 */
Timer.prototype.SetTimeout = function(ent, iid, funcname, time, data)
{
	let id = ++this.id;
	this.timers.set(id, [ent, iid, funcname, this.time + time, 0, data]);
	return id;
};

/**
 * Create a new repeating timer, which will call the 'funcname' method with arguments (data, lateness)
 * on the 'iid' component of the 'ent' entity, after at least 'time' milliseconds
 * and then every 'repeattime' milliseconds thereafter.
 * It will run multiple times per simulation turn if necessary.
 * 'repeattime' must be non-zero.
 * 'lateness' is how late the timer is executed after the specified time (in milliseconds).
 * Returns a non-zero id that can be passed to CancelTimer.
 */
Timer.prototype.SetInterval = function(ent, iid, funcname, time, repeattime, data)
{
	if (typeof repeattime != "number" || !(repeattime > 0))
		error("Invalid repeattime to SetInterval of " + funcname);
	let id = ++this.id;
	this.timers.set(id, [ent, iid, funcname, this.time + time, repeattime, data]);
	return id;
};

/**
 * Cancels an existing timer that was created with SetTimeout/SetInterval.
 */
Timer.prototype.CancelTimer = function(id)
{
	this.timers.delete(id);
};

Timer.prototype.OnUpdate = function(msg)
{
	this.turnLength = Math.round(msg.turnLength * 1000);
	this.time += this.turnLength;

	// Collect the timers that need to run
	// (We do this in two stages to avoid deleting from the timer list while
	// we're in the middle of iterating through it)
	let run = [];

	for (let id of this.timers.keys())
	{
		if (this.timers.get(id)[3] <= this.time)
			run.push(id);
	}

	for (let i = 0; i < run.length; ++i)
	{
		let id = run[i];
		let timer = this.timers.get(id);

		// An earlier timer might have cancelled this one, so skip it
		if (!timer)
			continue;

		let cmpTimer = Engine.QueryInterface(timer[0], timer[1]);
		if (!cmpTimer)
		{
			// The entity was probably destroyed; clean up the timer
			this.CancelTimer(id);
			continue;
		}

		try
		{
			cmpTimer[timer[2]](timer[5], this.time - timer[3]);
		}
		catch (e)
		{
			// Indent the stack trace
			let stack = e.stack.trimRight().replace(/^/mg, '  ');
			error("Error in timer on entity " + timer[0] + ", IID " + timer[1] + ", function " + timer[2] + ": " + e + "\n" + stack + "\n");
		}

		if (timer[4])
		{
			// Handle repeating timers
			// Add the repeat time to the execution time
			timer[3] += timer[4];
			// Add it to the list to get re-executed if it's soon enough
			if (timer[3] <= this.time)
				run.push(id);
		}
		else
		{
			// Non-repeating time - delete it
			this.CancelTimer(id);
		}
	}
};

Engine.RegisterSystemComponentType(IID_Timer, "Timer", Timer);
