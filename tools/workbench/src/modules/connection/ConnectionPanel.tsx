import {
  ConnectionPanel as SharedConnectionPanel,
  useConnectionOptions as useSharedOptions,
  type ConnectionOptions,
} from "@workbench/connections";
import { api, useWorkbench, connect } from "../../core/store";
export type { ConnectionOptions } from "@workbench/connections";
export function useConnectionOptions() {
  return useSharedOptions("nebulizer", { device: "STM32G070CB" });
}
export function ConnectionPanel({
  options,
  onChange,
}: {
  options: ConnectionOptions;
  onChange: (v: ConnectionOptions) => void;
}) {
  const state = useWorkbench();
  return (
    <SharedConnectionPanel
      options={options}
      onChange={onChange}
      state={state}
      api={api}
      onConnect={(value) => connect({ ...value })}
      notes={{
        swd: "SWD 调试不会烧录、暂停或复位 CPU；连接前逐段校验 ELF 与 Flash。",
        serial: "USART1：PB6 TX / PB7 RX，115200 / 8N1，接管后发送心跳。",
      }}
    />
  );
}
