# ============================================================
# Mycraft 发布打包脚本
# 用法：在项目根或 scripts 下执行
#   .\scripts\package.ps1
# 输出：dist\mycraft-v0.0.1-win64.zip
# ============================================================
$ErrorActionPreference = "Stop"

# ---- 配置 ----
$Version   = "0.1.0"
$Name      = "mycraft-v$Version-win64"
$ProjectRoot = Resolve-Path "$PSScriptRoot\.."
$SourceDir = Join-Path $ProjectRoot "build-release\Release"
$DistDir   = Join-Path $ProjectRoot "dist"
$StageDir  = Join-Path $DistDir $Name
$ZipPath   = Join-Path $DistDir "$Name.zip"

# ---- 前置检查 ----
if (-not (Test-Path "$SourceDir\mycraft.exe")) {
    Write-Error "找不到 $SourceDir\mycraft.exe，先跑 cmake --build build-release"
}
if (-not (Test-Path "$SourceDir\mycraft-server.exe")) {
    Write-Error "找不到 $SourceDir\mycraft-server.exe，先跑 cmake --build build-release"
}

# ---- 清理旧 staging ----
if (Test-Path $StageDir) { Remove-Item -Recurse -Force $StageDir }
if (-not (Test-Path $DistDir)) { New-Item -ItemType Directory -Path $DistDir | Out-Null }
New-Item -ItemType Directory -Path $StageDir | Out-Null

# ---- 拷可执行文件 ----
Copy-Item "$SourceDir\mycraft.exe"        $StageDir
Copy-Item "$SourceDir\mycraft-server.exe" $StageDir

# ---- 拷所有 DLL ----
Get-ChildItem "$SourceDir\*.dll" | ForEach-Object {
    Copy-Item $_.FullName $StageDir
}

# ---- 拷 assets ----
Copy-Item -Recurse "$SourceDir\assets" $StageDir

# ---- 写 CHANGELOG ----
$changelog = @"
Mycraft Changelog
=================

v$Version ($(Get-Date -Format "yyyy-MM-dd"))
----------------------------------------

引擎
- 构建系统支持 MSVC / Clang-cl (Windows) 与 GCC / Clang (Linux) 多平台编译
- 接入 ASan / UBSan / TSan 内存与线程检查，修复引擎关闭时序导致的 UAF
- 多级远景 LOD 系统（视锥剔除、垂直侧壁、移动感知调度）
- 抽象 IClientTransport，单人模式接入零延迟内存通信，支持网络热切换
- 服务端多线程 Chunk 生成（告别区块加载卡顿）
- 网络层扩展：玩家名字同步、掉落物多端同步

世界生成
- 洞穴生成（3D 噪声网格化优化）
- 草丛与花朵（十字交叉网格渲染 + 透明像素裁剪）
- 树木生成（保持原有多群系树木）
- 区块流式加载优化与动态视距调整

玩法
- 世界持久化存档（方块改动 changes.json + 玩家独立位置存储）
- 其他玩家方块人模型渲染
- 掉落物：Q 丢弃 / 自动捡拾 / 弹跳动画 / 多人同步
- 服务端权威物理 / 飞行模式（F） / 快速挖掘与放置

UI 与交互
- 重构主菜单：MC 风格自绘直角按钮、阴影文字、动态倾斜标语
- 现代化设置界面：深色极简风 + 青色高亮，左侧导航 + 右侧选项
- 重构 F3 极客调试面板：单行自适应暗底、左右分栏、抗高亮背景
- 优化退出逻辑：游戏中点 X 自动触发保存
"@
Set-Content -Path (Join-Path $StageDir "CHANGELOG.txt") -Value $changelog -Encoding UTF8

# ---- 打 zip ----
if (Test-Path $ZipPath) { Remove-Item -Force $ZipPath }
Compress-Archive -Path $StageDir -DestinationPath $ZipPath -CompressionLevel Optimal

# ---- 结果 ----
$sizeMB = [math]::Round((Get-Item $ZipPath).Length / 1MB, 2)
Write-Host ""
Write-Host "打包完成：" -ForegroundColor Green
Write-Host "  $ZipPath ($sizeMB MB)"