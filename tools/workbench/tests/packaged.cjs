const {_electron:electron}=require('playwright');
const path=require('node:path');
const assert=require('node:assert/strict');
async function main(){
  const root=path.join(__dirname,'..');
  const env={...process.env,WORKBENCH_PYTHON:'this-python-does-not-exist.exe'};
  delete env.ELECTRON_RUN_AS_NODE;delete env.WORKBENCH_DEV_URL;
  const app=await electron.launch({executablePath:path.join(root,'release/win-unpacked/Nebulizer Workbench.exe'),args:[],env,timeout:30000});
  try{
    const page=await app.firstWindow();
    await page.waitForFunction(()=>document.querySelector('.sidebar-footer .badge')?.textContent==='就绪',{},{timeout:30000});
    assert.equal(await app.evaluate(({app})=>app.isPackaged),true);
    const status=await page.evaluate(()=>window.workbench.request('status'));assert.equal(status.connected,false);
    const ports=await page.evaluate(()=>window.workbench.request('ports'));assert.ok(Array.isArray(ports));
    const image=path.resolve(root,'../../build/upgrade-release/nebulizer_factory.hex');
    const result=await page.evaluate(p=>window.workbench.request('inspect-factory',{path:p}),image);assert.equal(result.length,131072);
    await page.getByTestId('nav-factory').click();await page.getByRole('heading',{name:'工厂 HEX 烧录'}).waitFor();
    await page.screenshot({path:path.join(root,'test-results/packaged-factory.png'),fullPage:true});
    console.log('PASS: packaged Electron, bundled no-Qt backend, ports and factory validation; external Python disabled');
  }finally{await app.close()}
}
main().catch(e=>{console.error(e);process.exitCode=1});
