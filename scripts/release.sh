#!/bin/bash
# ============================================================
#  scripts/release.sh — 用 GitHub API 创建 Release
#
#  等价于 `gh release create`, 但不依赖 gh CLI, 只用 curl。
#  Token 通过隐藏输入获取, 不会写进 shell 历史。
#
#  用法 (在能访问 GitHub 的机器上, 如 Git Bash):
#     ./scripts/release.sh
#     ./scripts/release.sh --prerelease     # 标记为预发布 (推荐)
#     ./scripts/release.sh --notes-only     # 交互式粘贴说明
#
#  前置: 本机已配置好 git push 能访问 GitHub (PAT 已存进凭据管理器)
# ============================================================

set -e

RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
CYAN='\033[0;36m'
NC='\033[0m'

cd "$(dirname "$0")/.."

OWNER="Ricky944902"
REPO="jyfree"
PRERELEASE=1
TAG=""

while [ $# -gt 0 ]; do
    case "$1" in
        --prerelease) PRERELEASE=1; shift ;;
        --release)    PRERELEASE=0; shift ;;
        --owner)      OWNER="$2"; shift 2 ;;
        --repo)       REPO="$2"; shift 2 ;;
        --tag)        TAG="$2"; shift 2 ;;
        *) echo "未知参数: $1"; exit 1 ;;
    esac
done

# ---------- 环境检查 ----------
echo -e "${CYAN}"
echo "========================================"
echo "  jyfree Release 创建工具"
echo "========================================"
echo -e "${NC}"

command -v curl >/dev/null 2>&1 || {
    echo -e "${RED}缺少 curl${NC}"
    exit 1
}
echo "  curl: $(curl --version | head -1 | awk '{print $2}')"

# ---------- 版本与资产 ----------
VERSION=$(grep -oP '#define JIYU_VERSION_STRING "\K[^"]+' include/jiyu.h)
[ -z "$TAG" ] && TAG="v${VERSION}"

REL_NAME="jyfree-${VERSION}-aarch64"
TARBALL="release/${REL_NAME}.tar.gz"
SHAFILE="release/${REL_NAME}.tar.gz.sha256"
NOTES="release/RELEASE_NOTES.md"

echo "  仓库:  ${OWNER}/${REPO}"
echo "  Tag:   ${TAG}"
echo ""

for f in "$TARBALL" "$SHAFILE" "$NOTES"; do
    if [ ! -f "$f" ]; then
        echo -e "${RED}缺少文件: $f${NC}"
        echo "  先运行: ${CYAN}./scripts/make-release.sh${NC}"
        exit 1
    fi
done

echo "  资产:"
ls -lh "$TARBALL" "$SHAFILE" | awk '{print "    "$5"  "$9}'
echo ""

# ---------- TLS / 网络检查 ----------
# 背景: Windows 上常跑着 HTTPS 中间人工具 (如 Steam++/Watt Toolkit,
#       Clash 等), 它们用自己的根证书签发证书。git 可以切成 schannel
#       (走 Windows 证书库) 从而信任它们, 但 curl 用自带的 OpenSSL
#       CA bundle, 不认识这些根证书 → "unable to get local issuer"。
#       这里自动探测并处理, 不需要用户手工导出。

echo -e "${CYAN}[检查网络与 TLS]${NC}"

CURL_CA_OPT=()          # 最终传给 curl 的证书参数
CA_FILE=""             # 导出的 PEM 路径 (若有)

# 导出 Windows 证书库里的非系统根证书 (排除 Microsoft 自带的一大批),
# 只保留可能用于 HTTPS 中间人的那些 (按 Subject 关键字筛选)。
export_mitm_ca() {
    local out="$1"
    local ps_script='
$ErrorActionPreference = "SilentlyContinue"
$pat = "SteamTools|BeyondDimension|Clash|Mihomo|Verge|Watt|Proxy|Mitm|Burp|Fiddler|Charles|Netskope|Zscaler|cisco|Palo Alto|Fortinet|Sophos|Kaspersky|ESET|Avast|AVG|Malwarebytes|Comodo|Sectigo|RapidSSL|Symantec|Thawte|GeoTrust|Let.s Encrypt|ISRG|Root"
Get-ChildItem Cert:\CurrentUser\Root, Cert:\LocalMachine\Root |
  Where-Object { $_.Subject -match $pat } |
  ForEach-Object {
    if ($_.Subject -match "Microsoft") { return }
    "-----BEGIN CERTIFICATE-----"
    [Convert]::ToBase64String($_.RawData, "InsertLineBreaks")
    "-----END CERTIFICATE-----"
  }
'
    powershell -NoProfile -Command "$ps_script" 2>/dev/null > "$out"
    # 至少要有一个 PEM 块才算成功
    grep -q "BEGIN CERTIFICATE" "$out" 2>/dev/null
}

echo -n "  测试 api.github.com ... "

# 第一次: 用 curl 默认 CA
if RESP=$(curl -sS --max-time 15 -o /dev/null -w '%{http_code}' \
           "https://api.github.com" 2>/tmp/_curl_err); then
    echo -e "${GREEN}ok (HTTP $RESP)${NC}"
else
    echo -e "${YELLOW}失败${NC}"
    ERR=$(cat /tmp/_curl_err 2>/dev/null | head -2)
    echo "    原因: ${ERR:-未知}"

    # 判断是否证书问题
    if echo "$ERR" | grep -qiE 'certificate|issuer|SSL|TLS'; then
        echo ""
        echo -e "${YELLOW}  疑似 HTTPS 中间人证书问题 (常见于 Steam++/Clash 等)${NC}"
        echo "  尝试自动导出 Windows 证书库里的相关根证书..."

        # curl 的 --cacert 是"替换"默认 CA bundle, 不是追加。
        # 所以必须把默认 bundle 与导出的中间人根证书拼接起来,
        # 否则会丢掉 GitHub 自己的公共根证书。
        MITM_RAW="$HOME/.jyfree-mitm-raw.pem"
        CA_FILE="$HOME/.jyfree-ca.pem"
        DEFAULT_CA=""
        for c in /mingw64/etc/ssl/certs/ca-bundle.crt \
                 /usr/ssl/certs/ca-bundle.crt \
                 /etc/ssl/certs/ca-bundle.crt \
                 /mingw64/ssl/certs/ca-bundle.crt; do
            [ -f "$c" ] && DEFAULT_CA="$c" && break
        done

        if export_mitm_ca "$MITM_RAW"; then
            N_CA=$(grep -c "BEGIN CERTIFICATE" "$MITM_RAW")
            echo -e "${GREEN}    已导出 $N_CA 个根证书${NC}"

            if [ -n "$DEFAULT_CA" ]; then
                cat "$DEFAULT_CA" "$MITM_RAW" > "$CA_FILE"
                echo -e "${GREEN}    已与默认 bundle 合并 -> $CA_FILE${NC}"
            else
                cp "$MITM_RAW" "$CA_FILE"
                echo -e "${YELLOW}    未找到 curl 默认 CA bundle, 仅用导出证书${NC}"
            fi
            rm -f "$MITM_RAW"

            echo -n "  用合并后的证书重试 ... "
            if RESP=$(curl -sS --max-time 15 --cacert "$CA_FILE" \
                      -o /dev/null -w '%{http_code}' \
                      "https://api.github.com" 2>/tmp/_curl_err2); then
                echo -e "${GREEN}ok (HTTP $RESP)${NC}"
                CURL_CA_OPT=(--cacert "$CA_FILE")
            else
                echo -e "${RED}仍然失败${NC}"
                cat /tmp/_curl_err2 2>/dev/null | head -2 | sed 's/^/    /'
                rm -f "$CA_FILE" /tmp/_curl_err /tmp/_curl_err2
                exit 1
            fi
        else
            echo -e "${YELLOW}    未在证书库里找到匹配的根证书${NC}"
            echo ""
            echo -e "${YELLOW}    手动处理办法:${NC}"
            echo "      1) 若用的是 Clash/Steam++ 等, 先临时关闭其 HTTPS 解密功能"
            echo "      2) 或设置代理后用 https_proxy 环境变量:"
            echo "           ${CYAN}https_proxy=http://127.0.0.1:7890 ./scripts/release.sh${NC}"
            rm -f /tmp/_curl_err
            exit 1
        fi
    else
        echo ""
        echo -e "${YELLOW}    不是证书问题, 可能是:${NC}"
        echo "      - 网络不通 / 需要走代理 (试试 https_proxy=http://127.0.0.1:7890)"
        echo "      - 系统时间偏差过大 (GitHub API 对时间敏感, 偏差 >5 分钟会失败)"
        rm -f /tmp/_curl_err
        exit 1
    fi
fi

rm -f /tmp/_curl_err /tmp/_curl_err2
echo ""

# ---------- 获取 Token ----------
echo -e "${CYAN}[获取 Token]${NC}"
TOKEN="${GH_TOKEN:-}"

if [ -z "$TOKEN" ]; then
    echo "  需要一个 Personal Access Token 才能创建 Release"
    echo "  生成地址: ${CYAN}https://github.com/settings/tokens${NC}"
    echo "  (Fine-grained token 需勾 Repository permissions → Contents: Read and write)"
    echo ""
    echo "  粘贴 Token (输入不回显, 直接回车确认):"
    printf "  "
    read -rs TOKEN
    echo ""
fi

if [ -z "$TOKEN" ]; then
    echo -e "${RED}  Token 为空, 取消${NC}"
    exit 1
fi

# 先验证 token
AUTH_CHECK=$(curl -sS "${CURL_CA_OPT[@]}" -o /dev/null -w '%{http_code}' \
             -H "Authorization: Bearer $TOKEN" \
             -H "Accept: application/vnd.github+json" \
             "https://api.github.com/user")

if [ "$AUTH_CHECK" != "200" ]; then
    echo -e "${RED}  Token 验证失败 (HTTP $AUTH_CHECK)${NC}"
    echo "     检查: token 是否过期 / 权限是否足够"
    exit 1
fi

LOGIN=$(curl -sS "${CURL_CA_OPT[@]}" -H "Authorization: Bearer $TOKEN" \
        -H "Accept: application/vnd.github+json" \
        "https://api.github.com/user" | grep -oP '"login":\s*"\K[^"]+')
echo -e "${GREEN}  Token 有效 (${LOGIN})${NC}"
echo ""

# ---------- 检查 Tag 是否已存在 ----------
echo -e "${CYAN}[检查 Tag]${NC}"
EXIST=$(curl -sS "${CURL_CA_OPT[@]}" -o /dev/null -w '%{http_code}' \
        -H "Authorization: Bearer $TOKEN" \
        "https://api.github.com/repos/${OWNER}/${REPO}/git/ref/tags/${TAG}")

if [ "$EXIST" = "200" ]; then
    echo -e "${YELLOW}  Tag ${TAG} 已存在${NC}"
    read -r -p "  覆盖该 Release? [y/N] " ANS
    if [ "$ANS" != "y" ] && [ "$ANS" != "Y" ]; then
        echo "  已取消"
        exit 1
    fi
fi
echo ""

# ---------- 创建 Release ----------
echo -e "${CYAN}[创建 Release]${NC}"

PRERELEASE_JSON="false"
[ "$PRERELEASE" = "1" ] && PRERELEASE_JSON="true"

# 用 python 或 node 生成正确的 JSON (避免手工转义出错)
if command -v python3 >/dev/null 2>&1; then
    BODY=$(python3 -c "
import json,sys
print(json.dumps({
  'tag_name': '$TAG',
  'name': 'jyfree $VERSION — 极域绕过工具 (UOS/aarch64)',
  'prerelease': $([ "$PRERELEASE" = "1" ] && echo True || echo False),
  'draft': False,
  'body': open('$NOTES', encoding='utf-8').read()
}, ensure_ascii=False))
")
elif command -v node >/dev/null 2>&1; then
    BODY=$(node -e "
const fs=require('fs');
console.log(JSON.stringify({
  tag_name:'$TAG',
  name:'jyfree $VERSION — 极域绕过工具 (UOS/aarch64)',
  prerelease:$( [ "$PRERELEASE" = "1" ] && echo true || echo false ),
  draft:false,
  body:fs.readFileSync('$NOTES','utf8')
}));
")
else
    # 无 python/node 时用最简 JSON (说明会被截断)
    echo -e "${YELLOW}  未找到 python3/node, 说明将只含标题${NC}"
    BODY="{\"tag_name\":\"${TAG}\",\"name\":\"jyfree ${VERSION}\",\"prerelease\":${PRERELEASE_JSON},\"draft\":false}"
fi

RESP=$(curl -sS "${CURL_CA_OPT[@]}" -X POST \
       -H "Authorization: Bearer $TOKEN" \
       -H "Accept: application/vnd.github+json" \
       -H "Content-Type: application/json" \
       -d "$BODY" \
       "https://api.github.com/repos/${OWNER}/${REPO}/releases")

UPLOAD_URL=$(echo "$RESP" | grep -oP '"upload_url":\s*"\K[^"]+')
RELEASE_ID=$(echo "$RESP" | grep -oP '"id":\s*\K[0-9]+')

if [ -z "$UPLOAD_URL" ]; then
    echo -e "${RED}  创建失败:${NC}"
    echo "$RESP" | grep -oP '"message":\s*"\K[^"]+' | head -3
    exit 1
fi

echo -e "${GREEN}  Release 已创建 (ID=${RELEASE_ID})${NC}"

# upload_url 里带 {?name,label} 模板, 去掉
UPLOAD_URL=$(echo "$UPLOAD_URL" | sed 's/{.*}$//')
echo ""

# ---------- 上传资产 ----------
echo -e "${CYAN}[上传资产]${NC}"
for asset in "$TARBALL" "$SHAFILE"; do
    NAME=$(basename "$asset")
    SIZE=$(du -h "$asset" | cut -f1)
    printf "  %-40s %s  " "$NAME" "$SIZE"

    CODE=$(curl -sS "${CURL_CA_OPT[@]}" -o /tmp/_rel_out -w '%{http_code}' \
           -X POST \
           -H "Authorization: Bearer $TOKEN" \
           -H "Content-Type: application/octet-stream" \
           -H "Content-Length: $(wc -c < "$asset" | tr -d ' ')" \
           --data-binary "@$asset" \
           "$UPLOAD_URL?name=$NAME")

    if [ "$CODE" = "201" ] || [ "$CODE" = "200" ]; then
        echo -e "${GREEN}ok${NC}"
    else
        echo -e "${RED}失败 (HTTP $CODE)${NC}"
        grep -oP '"message":\s*"\K[^"]+' /tmp/_rel_out 2>/dev/null | head -1
    fi
done
rm -f /tmp/_rel_out
echo ""

# ---------- 完成 ----------
echo -e "${GREEN}"
echo "========================================"
echo "  Release 创建完成"
echo "========================================"
echo -e "${NC}"
echo ""
echo "  地址: https://github.com/${OWNER}/${REPO}/releases/tag/${TAG}"
echo ""
if [ "$PRERELEASE" = "1" ]; then
    echo -e "${CYAN}  已标记为 Pre-release (推荐)${NC}"
    echo -e "${CYAN}  代码未经真机验证, 不应标为正式版${NC}"
else
    echo -e "${YELLOW}  你选择了正式发布 (--release)${NC}"
    echo -e "${YELLOW}  提醒: 代码从未在真机运行过${NC}"
fi
echo ""
echo "  验证:"
echo "    curl -s https://api.github.com/repos/${OWNER}/${REPO}/releases/tags/${TAG} \\"
echo "      | grep -oP '\"browser_download_url\":\\s*\"\\K[^\"]+'"