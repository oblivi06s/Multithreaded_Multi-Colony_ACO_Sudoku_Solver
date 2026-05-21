/* Shared solver API helpers for Experiment, Game, and Create views. */
(function (global) {
  "use strict";

  async function readJsonSafe(response) {
    const text = await response.text();
    if (!text) return {};
    try {
      return JSON.parse(text);
    } catch (_) {
      return {};
    }
  }

  async function startSolve(payload, signal) {
    const response = await fetch("/api/solve", {
      method: "POST",
      headers: { "Content-Type": "application/json" },
      body: JSON.stringify(payload),
      signal: signal || undefined,
    });
    const data = await readJsonSafe(response);
    if (!response.ok) {
      throw new Error(data.error || ("Server error " + response.status));
    }
    return data;
  }

  async function fetchStatus(jobId, signal) {
    const response = await fetch("/api/status/" + encodeURIComponent(jobId), {
      signal: signal || undefined,
    });
    const data = await readJsonSafe(response);
    if (!response.ok) {
      throw new Error(data.error || ("Status " + response.status));
    }
    return data;
  }

  async function stopSolve(jobId) {
    const response = await fetch("/api/stop/" + encodeURIComponent(jobId), {
      method: "POST",
      headers: { "Content-Type": "application/json" },
    });
    const data = await readJsonSafe(response);
    if (!response.ok) {
      throw new Error(data.error || ("Stop error " + response.status));
    }
    return data;
  }

  async function pollJob(jobId, options) {
    const opts = options || {};
    const deadlineMs = Date.now() + (opts.timeoutMs || 120000);
    const intervalMs = opts.intervalMs || 150;
    const signal = opts.signal;
    const onUpdate = typeof opts.onUpdate === "function" ? opts.onUpdate : null;

    return new Promise(function (resolve, reject) {
      const poll = async function () {
        try {
          if (Date.now() > deadlineMs) {
            reject(new Error("Request timed out"));
            return;
          }
          const data = await fetchStatus(jobId, signal);
          if (onUpdate) onUpdate(data);
          if (data.status === "done" || data.status === "error" || data.status === "stopped") {
            resolve(data);
            return;
          }
          setTimeout(poll, intervalMs);
        } catch (err) {
          reject(err);
        }
      };
      poll();
    });
  }

  global.SolverService = {
    startSolve: startSolve,
    fetchStatus: fetchStatus,
    stopSolve: stopSolve,
    pollJob: pollJob,
  };
})(window);
