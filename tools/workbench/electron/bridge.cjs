const { spawn } = require("node:child_process");
const { EventEmitter } = require("node:events");
const readline = require("node:readline");
const path = require("node:path");
const fs = require("node:fs");
class HardwareBridge extends EventEmitter {
  constructor({ packaged, resources, root }) {
    super();
    this.settings = { packaged, resources, root };
    this.pending = new Map();
    this.id = 0;
    this.child = null;
    this.closing = false;
    this.busy = false;
    this.connected = false;
  }
  start() {
    if (this.child) return;
    const { packaged, resources, root } = this.settings;
    const binary = path.join(resources, "device-backend", "device-backend.exe");
    const command = packaged
      ? binary
      : process.env.WORKBENCH_PYTHON || "python";
    const args = packaged
      ? []
      : ["-u", path.join(root, "backend", "server.py")];
    if (packaged && !fs.existsSync(binary))
      throw Error("缺少设备后台程序，请重新打包");
    this.child = spawn(command, args, {
      cwd: packaged ? resources : root,
      windowsHide: true,
      stdio: ["pipe", "pipe", "pipe"],
      env: { ...process.env, PYTHONIOENCODING: "utf-8", PYTHONUTF8: "1" },
    });
    readline
      .createInterface({ input: this.child.stdout })
      .on("line", (line) => {
        try {
          const msg = JSON.parse(line);
          if (msg.event) {
            if (msg.event === "job") this.busy = !!msg.data.running;
            if (msg.event === "connection")
              this.connected = !!msg.data.connected;
            this.emit("event", msg.event, msg.data);
          } else {
            const task = this.pending.get(msg.id);
            if (task) {
              clearTimeout(task.timer);
              this.pending.delete(msg.id);
              msg.error
                ? task.reject(Error(msg.error))
                : task.resolve(msg.result);
            }
          }
        } catch {
          this.emit("event", "log", {
            level: "error",
            message: "后台输出无法解析：" + line.slice(0, 400),
          });
        }
      });
    this.child.stderr.on("data", (data) =>
      this.emit("event", "log", {
        level: "info",
        message: String(data).trim().slice(0, 2000),
      }),
    );
    const failed = (error) => {
      for (const task of this.pending.values()) {
        clearTimeout(task.timer);
        task.reject(error);
      }
      this.pending.clear();
      this.child = null;
      this.connected = false;
      this.busy = false;
      this.emit("event", "connection", {
        connected: false,
        error: error.message,
      });
      this.emit("event", "backend-exit", { error: error.message });
    };
    this.child.on("error", (e) =>
      failed(Error("设备后台无法启动：" + e.message)),
    );
    this.child.on("exit", (code) =>
      failed(
        Error(
          this.closing
            ? "设备后台已关闭"
            : `设备后台退出 (${code})；请检查 Python 依赖或重新启动应用`,
        ),
      ),
    );
  }
  request(method, params = {}) {
    const job = method === "factory" || method === "update";
    if (job && this.busy) return Promise.reject(Error("已有安装作业"));
    this.start();
    const id = ++this.id;
    if (job) this.busy = true;
    return new Promise((resolve, reject) => {
      // Long factory writes never get force-terminated on a response timeout.
      const timer = setTimeout(
        () => {
          this.pending.delete(id);
          reject(Error("后台应答超时；设备结果未知，请勿重复安装"));
        },
        method === "factory" || method === "update"
          ? 15 * 60 * 1000
          : 90 * 1000,
      );
      this.pending.set(id, {
        resolve: (value) => {
          if (job) this.busy = false;
          resolve(value);
        },
        reject: (error) => {
          if (job) this.busy = false;
          reject(error);
        },
        timer,
      });
      this.child.stdin.write(
        JSON.stringify({ id, method, params }) + "\n",
        (error) => {
          if (error) {
            clearTimeout(timer);
            this.pending.delete(id);
            reject(error);
          }
        },
      );
    });
  }
  async close() {
    this.closing = true;
    if (this.child) {
      await this.request("shutdown").catch(() => {});
      this.child?.stdin.end();
    }
  }
}
module.exports = { HardwareBridge };
