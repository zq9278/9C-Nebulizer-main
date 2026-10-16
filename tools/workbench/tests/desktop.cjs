/* Real Electron/preload/backend smoke test. Synthetic snapshots are test-only. */
const { _electron: electron } = require("playwright");
const path = require("node:path");
const fs = require("node:fs/promises");
const assert = require("node:assert/strict");
async function main() {
  const root = path.join(__dirname, ".."),
    env = { ...process.env };
  delete env.ELECTRON_RUN_AS_NODE;
  const app = await electron.launch({ args: [root], env, timeout: 30000 });
  const page = await app.firstWindow();
  const errors = [];
  page.on("pageerror", (e) => {
    errors.push(e.message);
    console.error(e.stack);
  });
  try {
    await page.getByText("本地硬件服务").waitFor();
    await page.waitForFunction(
      () =>
        document.querySelector(".sidebar-footer .badge")?.textContent ===
        "就绪",
      {},
      { timeout: 20000 },
    );
    assert.equal(await page.evaluate(() => typeof window.require), "undefined");
    const status = await page.evaluate(() =>
      window.workbench.request("status"),
    );
    assert.equal(status.connected, false);
    const ports = await page.evaluate(() => window.workbench.request("ports"));
    assert.ok(Array.isArray(ports));
    const file = path.resolve(
      root,
      "../../build/upgrade-release/nebulizer_factory.hex",
    );
    const inspected = await page.evaluate(
      (p) => window.workbench.request("inspect-factory", { path: p }),
      file,
    );
    assert.equal(inspected.length, 131072);
    await page.getByTestId("nav-parameters").click();
    await page.getByRole("heading", { name: "参数工作区" }).waitFor();
    const temp = page.locator("input[type=number]").first();
    await temp.fill("39.2");
    await page.getByTestId("nav-monitor").click();
    await page.getByTestId("nav-parameters").click();
    assert.equal(
      await page.locator("input[type=number]").first().inputValue(),
      "39.2",
    );
    for (const id of [
      "maintenance",
      "trends",
      "update",
      "factory",
      "logs",
      "about",
    ]) {
      await page.getByTestId("nav-" + id).click();
      await page.waitForTimeout(60);
    }
    await page.getByTestId("nav-factory").click();
    assert.equal(
      await page.getByRole("button", { name: "烧录工厂镜像" }).isDisabled(),
      true,
    );
    const rejected = await page.evaluate(async (p) => {
      try {
        await window.workbench.request("factory", {
          path: p,
          confirmed: false,
        });
        return false;
      } catch {
        return true;
      }
    }, file);
    assert.equal(rejected, true);
    await page.getByTestId("nav-monitor").click();
    await fs.mkdir(path.join(root, "test-results"), { recursive: true });
    await page.screenshot({
      path: path.join(root, "test-results/desktop-offline.png"),
      fullPage: true,
    });
    // Explicitly inject renderer samples only. No fake controller state reaches installation RPC.
    const snapshot = {
      uptime_ms: 0,
      sequence: 1,
      owner: 0,
      owner_alive: false,
      control: false,
      config: {
        mode: 0,
        target_temp_deci_c: 420,
        duration_sec: 600,
        air_level: 2,
        mist_level: 2,
        keep_warm_enabled: 0,
      },
      pids: Object.fromEntries(
        ["heat", "fan", "preheat"].map((g) => [
          g,
          {
            kp_milli: 9000,
            ki_milli: 0,
            kd_milli: 0,
            integral_limit_permille: 400,
          },
        ]),
      ),
      runtime: {
        state: 2,
        remaining_sec: 600,
        fault: 0,
        heartbeat: 1,
        temperatures: [361, 290, 304, 527],
        adc: [1200, 1300, 1400, 1500],
        sample_ms: 0,
        phase: 0,
        heat_output: 0,
        fan_output: 0,
        fan_base: 0,
        fan_boost: 0,
        fan_error: 0,
        heat_error: 59,
        heat_target: 420,
        heat_integral: 0,
        fan_integral: 0,
        mist_online: 1,
        mist_running: 0,
        mist_actual: 0,
        mist_fault: 0,
        cover: 1,
        liquid: 1,
        gx1832: 1,
      },
      maintenance: { active: 0, fan: 0, mist: 0, heat: 0 },
    };
    await app.evaluate(({ BrowserWindow }, sample) => {
      const w = BrowserWindow.getAllWindows()[0];
      for (let i = 0; i < 600; i++) {
        sample.uptime_ms = i * 100;
        sample.sequence = i + 1;
        sample.runtime.temperatures[0] = Math.round(
          350 + 15 * Math.sin(i / 50),
        );
        w.webContents.send("workbench:event", {
          name: "snapshot",
          data: structuredClone(sample),
        });
      }
    }, snapshot);
    await page.waitForTimeout(500);
    assert.equal(
      await page.getByRole("button", { name: "开始治疗" }).isDisabled(),
      true,
    );
    await page.screenshot({
      path: path.join(root, "test-results/desktop-samples.png"),
      fullPage: true,
    });
    assert.deepEqual(errors, []);
    console.log(
      "PASS: real Electron, isolated preload, Python RPC, module navigation, draft persistence, confirmation guards, chart rendering",
    );
  } finally {
    await app.close();
  }
}
main().catch((e) => {
  console.error(e);
  process.exitCode = 1;
});
