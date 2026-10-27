#!/usr/bin/env bash
set -euo pipefail
: "${GITHUB_REPOSITORY:?}" "${GITHUB_SHA:?}" "${IMAGE:?}" "${CANN_VERSION:?}" "${DIGEST:?}" "${GITHUB_STEP_SUMMARY:?}"
[[ "$DIGEST" =~ ^sha256:[0-9a-f]{64}$ ]] || { echo 'Invalid image digest' >&2; exit 1; }
current=$(gh api "repos/$GITHUB_REPOSITORY/git/ref/heads/master" --jq '.object.sha')
if [[ "$current" != "$GITHUB_SHA" ]]; then
    echo 'Master has advanced; keep the commit image without changing latest.' >> "$GITHUB_STEP_SUMMARY"
    exit 0
fi
skopeo copy --all --preserve-digests \
    --authfile "${DOCKER_CONFIG:-$HOME/.docker}/config.json" \
    "docker://$IMAGE@$DIGEST" "docker://$IMAGE:latest-cann$CANN_VERSION"
printf '%s:latest-cann%s@%s\n' "$IMAGE" "$CANN_VERSION" "$DIGEST" >> "$GITHUB_STEP_SUMMARY"
