import { useEffect, useState } from 'react'

export const REPO = 'https://github.com/mrAbhimanyuVishwakarma/SmoothCam'
export const RELEASES = `${REPO}/releases`
export const LATEST_PAGE = `${RELEASES}/latest`

// Always offer the newest release: ask GitHub which installer the latest
// release has and link straight to it. Until that answer arrives (or if
// GitHub can't be reached) the link opens the latest release page.
export function useLatestRelease() {
  const [release, setRelease] = useState({ url: LATEST_PAGE, version: null, sizeMb: null, sha256: null })

  useEffect(() => {
    let cancelled = false
    fetch('https://api.github.com/repos/mrAbhimanyuVishwakarma/SmoothCam/releases/latest', {
      headers: { Accept: 'application/vnd.github+json' },
    })
      .then((res) => (res.ok ? res.json() : null))
      .then((data) => {
        if (!data || cancelled) return
        const exe = (data.assets || []).find((a) => /\.exe$/i.test(a.name))
        if (!exe) return
        setRelease({
          url: exe.browser_download_url,
          version: String(data.tag_name || '').replace(/^v/i, ''),
          sizeMb: (exe.size / 1048576).toFixed(1),
          sha256:
            typeof exe.digest === 'string' && exe.digest.startsWith('sha256:')
              ? exe.digest.slice(7).toUpperCase()
              : null,
        })
      })
      .catch(() => {})
    return () => {
      cancelled = true
    }
  }, [])

  return release
}
