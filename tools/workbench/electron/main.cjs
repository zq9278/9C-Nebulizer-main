const { app, BrowserWindow, ipcMain, dialog } = require("electron");
const path = require("node:path");
const fs = require("node:fs/promises");
const { HardwareBridge } = require("./bridge.cjs");
let window,
  bridge,
  allowClose = false,
  record = null,
  recordQueue = Promise.resolve();
const methods = new Set([
  "status",
  "ports",
  "probes",
  "connect",
  "disconnect",
  "commands",
  "validate-params",
  "inspect-factory",
  "inspect-update",
  "factory",
  "update",
  "cancel",
]);
function checkSender(event) {
  if (event.sender !== window?.webContents) throw Error("无效的调用来源");
}
app.whenReady().then(() => {
  bridge = new HardwareBridge({
    packaged: app.isPackaged,
    resources: process.resourcesPath,
    root: path.join(__dirname, ".."),
  });
  window = new BrowserWindow({
    width: 1530,
    height: 1010,
    minWidth: 1100,
    minHeight: 760,
    title: "Nebulizer Workbench",
    backgroundColor: "#f5f7fa",
    webPreferences: {
      preload: path.join(__dirname, "preload.cjs"),
      contextIsolation: true,
      nodeIntegration: false,
      sandbox: true,
    },
  });
  window.webContents.setWindowOpenHandler(() => ({ action: "deny" }));
  window.webContents.on("will-navigate", (event) => event.preventDefault());
  window.webContents.session.setPermissionRequestHandler(
    (_wc, _permission, callback) => callback(false),
  );
  bridge.on("event", (name, data) => {
    if (!window.isDestroyed())
      window.webContents.send("workbench:event", { name, data });
  });
  ipcMain.handle("workbench:request", (event, method, params) => {
    checkSender(event);
    if (!methods.has(method)) throw Error("不允许的方法");
    if (JSON.stringify(params || {}).length > 1024 * 1024)
      throw Error("请求过大");
    return bridge.request(method, params);
  });
  ipcMain.handle("workbench:open", async (event, kind) => {
    checkSender(event);
    const extensions = {
      elf: ["elf"],
      hex: ["hex"],
      nfu: ["nfu"],
      json: ["json"],
      dll: ["dll"],
    };
    if (!extensions[kind]) throw Error("无效文件类型");
    const r = await dialog.showOpenDialog(window, {
      properties: ["openFile"],
      filters: [{ name: kind.toUpperCase(), extensions: extensions[kind] }],
    });
    return r.canceled ? null : r.filePaths[0];
  });
  ipcMain.handle("workbench:save", async (event, name, content) => {
    checkSender(event);
    if (typeof content !== "string" || content.length > 200 * 1024 * 1024)
      throw Error("导出内容过大");
    const r = await dialog.showSaveDialog(window, {
      defaultPath: path.basename(String(name)),
    });
    if (r.canceled) return null;
    await fs.writeFile(r.filePath, content, "utf8");
    return r.filePath;
  });
  ipcMain.handle("workbench:read", async (event, file) => {
    checkSender(event);
    if (typeof file !== "string" || !file.endsWith(".json"))
      throw Error("仅支持读取 JSON 参数");
    if ((await fs.stat(file)).size > 1024 * 1024) throw Error("参数文件过大");
    return fs.readFile(file, "utf8");
  });
  ipcMain.handle("workbench:record", async (event, action, payload) => {
    checkSender(event);
    if (action === "start") {
      if (record) throw Error("正在记录");
      const r = await dialog.showSaveDialog(window, {
        defaultPath: "nebulizer-recording.csv",
      });
      if (r.canceled) return null;
      record = await fs.open(r.filePath, "w");
      recordQueue = Promise.resolve();
      await record.write("\uFEFF" + String(payload));
      return r.filePath;
    }
    if (action === "append") {
      if (typeof payload !== "string" || payload.length > 16384)
        throw Error("记录行无效");
      const handle = record;
      if (handle) {
        recordQueue = recordQueue.then(() => handle.write(payload));
        await recordQueue;
      }
      return !!handle;
    }
    if (action === "stop") {
      await recordQueue.catch(() => {});
      if (record) {
        await record.close();
        record = null;
      }
      return true;
    }
    throw Error("未知记录操作");
  });
  window.on("close", (event) => {
    if (allowClose) return;
    event.preventDefault();
    if (bridge.busy) {
      dialog.showMessageBox(window, {
        type: "info",
        message: "设备安装作业进行中，请等待完成；串口升级可以在升级页面取消。",
      });
      return;
    }
    window.webContents.send("workbench:event", { name: "closing", data: null });
    bridge.close().finally(async () => {
      await recordQueue.catch(() => {});
      if (record) await record.close();
      allowClose = true;
      window.close();
    });
  });
  const dev = process.env.WORKBENCH_DEV_URL;
  (dev
    ? window.loadURL(dev)
    : window.loadFile(path.join(__dirname, "../dist/index.html"))
  ).then(() => bridge.start());
});
app.on("window-all-closed", () => app.quit());
