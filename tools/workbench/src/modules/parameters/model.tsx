import {
  createContext,
  useContext,
  useState,
  useEffect,
  type ReactNode,
} from "react";
import { useWorkbench, request, batch } from "../../core/store";
import {
  defaultParams,
  paramsFrom,
  configCommands,
  pidCommand,
  pidGroups,
  cmd,
  type Params,
  type Snapshot,
} from "../../projects/nebulizer";
const readCommands = [0x24, 0x27, 0x28, 0x33, 0x31, 0x2a, 0x30].map((n) =>
  cmd(n),
);
function useModel() {
  const s = useWorkbench();
  const [params, setParams] = useState<Params>(() => {
    try {
      return {
        ...defaultParams,
        ...JSON.parse(localStorage.getItem("nebulizer/draft") || "{}"),
      };
    } catch {
      return defaultParams;
    }
  });
  const [dirty, setDirty] = useState<string[]>(() =>
      localStorage.getItem("nebulizer/draft-dirty") === "1"
        ? ["config", "heat", "fan", "preheat"]
        : [],
    ),
    [initialized, setInitialized] = useState(false);
  useEffect(() => {
    localStorage.setItem("nebulizer/draft", JSON.stringify(params));
    localStorage.setItem("nebulizer/draft-dirty", dirty.length ? "1" : "0");
  }, [params, dirty]);
  useEffect(() => {
    if (!s.connected) setInitialized(false);
    else if (s.snapshot && !initialized) {
      if (!dirty.length) setParams(paramsFrom(s.snapshot));
      setInitialized(true);
    }
  }, [s.connected, s.snapshot, initialized]);
  function edit(key: string, value: number | boolean, group = "config") {
    setParams((p) => {
      const next = { ...p, [key]: value };
      if (key === "mode" && value === 1) next.keep_warm_enabled = false;
      return next;
    });
    setDirty((d) => (d.includes(group) ? d : [...d, group]));
  }
  function load(snapshot: Snapshot, group?: string) {
    const p = paramsFrom(snapshot);
    if (!group) {
      setParams(p);
      setDirty([]);
    } else {
      const keys =
        group === "config"
          ? [
              "mode",
              "target_temp_deci_c",
              "duration_min",
              "air_level",
              "mist_level",
              "keep_warm_enabled",
            ]
          : ["kp", "ki", "kd", "i_limit_permille"].map(
              (k) => pidGroups.find((g) => g.id === group)!.prefix + k,
            );
      setParams((prev) => ({
        ...prev,
        ...Object.fromEntries(keys.map((k) => [k, p[k]])),
      }));
      setDirty((d) => d.filter((k) => k !== group));
    }
  }
  async function read(group?: string) {
    const snapshot = await batch(readCommands);
    if (snapshot) load(snapshot, group);
  }
  async function apply(group: string) {
    await request("validate-params", { params });
    const commands =
      group === "config"
        ? configCommands(params)
        : [
            pidCommand(
              pidGroups.find((g) => g.id === group)!,
              params,
            ),
          ];
    const result = await batch(commands);
    if (result) load(result, group);
  }
  async function start() {
    await request("validate-params", { params });
    const result = await batch([
      ...(dirty.includes("config") ? configCommands(params) : []),
      cmd(0x20),
    ]);
    if (result && dirty.includes("config")) load(result, "config");
  }
  async function importParams(value: unknown) {
    const p = await request<Params>("validate-params", { params: value });
    setParams(p);
    setDirty(["config", "heat", "fan", "preheat"]);
  }
  return { params, dirty, edit, read, apply, start, importParams };
}
const Context = createContext<ReturnType<typeof useModel> | null>(null);
export function ParametersProvider({ children }: { children: ReactNode }) {
  const value = useModel();
  return <Context.Provider value={value}>{children}</Context.Provider>;
}
export function useParameters() {
  return useContext(Context)!;
}
