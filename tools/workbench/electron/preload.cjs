const { contextBridge, ipcRenderer } = require("electron");
contextBridge.exposeInMainWorld("workbench", {
  request: (method, params) =>
    ipcRenderer.invoke("workbench:request", method, params),
  subscribe: (listener) => {
    const handler = (_event, { name, data }) => listener(name, data);
    ipcRenderer.on("workbench:event", handler);
    return () => ipcRenderer.removeListener("workbench:event", handler);
  },
  openFile: (kind) => ipcRenderer.invoke("workbench:open", kind),
  saveFile: (name, content) =>
    ipcRenderer.invoke("workbench:save", name, content),
  readFile: (path) => ipcRenderer.invoke("workbench:read", path),
  recording: (action, payload) =>
    ipcRenderer.invoke("workbench:record", action, payload),
});
