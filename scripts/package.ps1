# ============================================================
# Mycraft 发布打包脚本
# 用法：在项目根或 scripts 下执行
#   .\scripts\package.ps1
# 输出：dist\mycraft-v0.0.1-win64.zip
# ============================================================
$ErrorActionPreference = "Stop"

# ---- 配置 ----
$Version   = "0.0.1"
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
- IRenderAPI 抽象，OpenGL / Vulkan 双后端
- Vulkan 动态渲染 + FBO 后处理
- 一份 GLSL 源，两个编译目标（VK / GL）
- 网络层（NetworkServer / NetworkClient / MessageReader / MessageWriter）
- Perlin 噪声（2D / 3D / Fractal 叠加）
- ImGui 集成（两后端）

世界生成
- 3D 密度地形（样条 + Domain warp + 三线性插值）
- 生物群系：海洋 / 沙滩 / 沙漠 / 雪原 / 山地 / 森林 / 平原
- 树木生成
- 矿脉生成（煤 / 铁）
- 区块流式加载（R=16）

玩法
- 服务端权威玩家位置 + 客户端本地预测
- 玩家物理：重力 / AABB 碰撞 / 跳跃 / 飞行模式（F）
- 掉落物：Q 丢弃 / 自动捡拾 / 弹跳动画
- 方块交互：射线检测 + 挖 / 放
- 主菜单 / 单人 / 多人模式
- 独立服务端 exe（mycraft-server）
- F3 调试面板 / ESC 暂停菜单 / 准心 / hotbar
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