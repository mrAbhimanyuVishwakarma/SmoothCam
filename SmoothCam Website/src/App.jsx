import { lazy, Suspense, useEffect, useRef, useState } from 'react'
import {
  AnimatePresence,
  animate,
  motion,
  useInView,
  useMotionValueEvent,
  useScroll,
  useSpring,
  useTransform,
} from 'framer-motion'
import {
  Aperture,
  Check,
  ChevronDown,
  Columns2,
  Download,
  EyeOff,
  Gauge,
  HardDrive,
  Lock,
  Maximize2,
  Play,
  Power,
  ScanFace,
  ShieldCheck,
  Sparkles,
  Sun,
} from 'lucide-react'
import { REPO, RELEASES, useLatestRelease } from './useLatestRelease'

const Hero3D = lazy(() => import('./Hero3D.jsx'))

const hasWebGL = (() => {
  try {
    const canvas = document.createElement('canvas')
    return !!(canvas.getContext('webgl2') || canvas.getContext('webgl'))
  } catch {
    return false
  }
})()
const ease = [0.22, 1, 0.36, 1]

// ---------------------------------------------------------------- content

const FEATURES = [
  { icon: Gauge, title: 'Up to 60 fps', text: 'Motion-estimated frame generation turns a 24 or 30 fps webcam into a steady 60.' },
  { icon: Maximize2, title: 'Upscale to 4K', text: 'Five open-source upscalers: FSR 1, NIS, CAS, Lanczos or Auto. None adds a frame of delay.' },
  { icon: Sparkles, title: 'Noise reduction', text: 'Grain and colour noise disappear, without smearing anything that moves.' },
  { icon: Sun, title: 'Low-light boost', text: 'Brightens dim rooms in software, so the camera never has to slow down.' },
  { icon: Aperture, title: 'Background blur', text: 'On-device segmentation whose edge follows your hair and shoulders, with no halo.' },
  { icon: ScanFace, title: 'Auto-framing', text: 'Follows your face and zooms like a camera operator: gentle starts, no jitter.' },
  { icon: Columns2, title: 'Compare view', text: 'Your raw camera on the left, SmoothCam on the right, each labelled with its fps.' },
  { icon: Power, title: 'One switch', text: 'Flip it off and every app instantly gets your plain camera back.' },
]

const STAGES = [
  { k: 'Capture', title: 'Your webcam, as it is', text: 'SmoothCam opens your real camera: 24 or 30 fps, often noisy, sometimes dark.', metric: '30 fps' },
  { k: 'Motion', title: 'Motion estimation', text: 'For every pair of frames the GPU works out how each part of the picture moved, on NVIDIA optical-flow hardware or its own compute shader.', metric: 'GPU' },
  { k: 'Generate', title: 'Frame generation', text: 'The missing in-between frames are drawn along that motion, with a soft fallback wherever motion doesn’t make sense.', metric: '60 fps' },
  { k: 'Upscale', title: 'Upscaling', text: 'FSR 1, NIS, CAS or Lanczos take the picture past your camera’s own resolution.', metric: '4K' },
  { k: 'Effects', title: 'Effects', text: 'Noise reduction, low-light boost, background blur and auto-framing, each skipped when it’s off.', metric: 'Local' },
  { k: 'Deliver', title: 'Every app', text: 'Teams, Zoom, Meet, Discord and OBS simply see a camera called SmoothCam.', metric: '1 cam' },
]

const UPSCALERS = [
  { name: 'Auto', by: 'Recommended', text: 'AMD CAS up to 2×, FSR 1 beyond. The best all-rounder in our tests.', best: true },
  { name: 'AMD FSR 1', by: 'AMD FidelityFX', text: 'Edge-adaptive upscaling with noise-aware sharpening, at any ratio.' },
  { name: 'NVIDIA NIS', by: 'NVIDIA Image Scaling', text: 'A directional scaler with built-in sharpening. Runs on any GPU.' },
  { name: 'AMD CAS', by: 'AMD FidelityFX', text: 'Upscale and sharpen in one light pass. Easy on older GPUs.' },
  { name: 'Lanczos', by: 'Classic', text: 'The classic resampler, with anti-ringing. Lovely on soft webcam footage.' },
  { name: 'Off', by: 'Purist', text: 'No upscaling. Only your camera’s real resolutions are offered.' },
]

const FAQS = [
  { q: 'Does it add lag?', a: 'A little in Quality mode: to draw a frame between two real ones, SmoothCam needs the second one. At 30 fps that’s about one camera frame plus a few milliseconds, very hard to notice in a call. Low latency mode cuts it to about 20 ms.' },
  { q: 'Do I need an NVIDIA graphics card?', a: 'No. NVIDIA cards (GTX 16 series and newer) use their optical-flow hardware; every other DirectX 11 GPU uses SmoothCam’s own GPU compute path. All upscalers and effects work on any GPU.' },
  { q: 'Will it make a bad webcam look sharp?', a: 'It makes motion smoother and edges cleaner, and noise reduction plus low-light boost help a lot in dim rooms. It can’t invent detail the camera never captured; good lighting still beats everything.' },
  { q: 'Can I use my phone as the camera?', a: 'Not through Phone Link yet: Windows connects a phone as a virtual camera, and it doesn’t let one virtual camera read another. A USB webcam works, and so does a phone app that shows up as a USB webcam.' },
  { q: 'Which apps does it work with?', a: 'Apps that use Windows cameras, like Teams, Zoom, Google Meet in your browser, Discord, OBS and the Windows Camera app. Just choose “SmoothCam” as the camera.' },
  { q: 'How do I uninstall it?', a: 'Settings → Apps → Installed apps → SmoothCam → Uninstall. The virtual camera is removed with it.' },
]

const APPS = ['Microsoft Teams', 'Zoom', 'Google Meet', 'Discord', 'OBS Studio', 'Windows Camera']

// ---------------------------------------------------------------- building blocks

function Reveal({ children, delay = 0, y = 36, className }) {
  return (
    <motion.div
      className={className}
      initial={{ opacity: 0, y }}
      whileInView={{ opacity: 1, y: 0 }}
      viewport={{ once: true, amount: 0.25 }}
      transition={{ duration: 0.8, delay, ease }}
    >
      {children}
    </motion.div>
  )
}

function SectionHead({ eyebrow, title, sub, center }) {
  return (
    <div className={`section-head${center ? ' section-head--center' : ''}`}>
      <Reveal><span className="eyebrow">{eyebrow}</span></Reveal>
      <Reveal delay={0.06}><h2 className="h2">{title}</h2></Reveal>
      {sub && <Reveal delay={0.12}><p className="sub">{sub}</p></Reveal>}
    </div>
  )
}

function ScrollProgress() {
  const { scrollYProgress } = useScroll()
  const scaleX = useSpring(scrollYProgress, { stiffness: 140, damping: 30, mass: 0.3 })
  return <motion.div className="progress" style={{ scaleX }} />
}

function Nav({ release }) {
  const { scrollY } = useScroll()
  const [scrolled, setScrolled] = useState(false)
  useMotionValueEvent(scrollY, 'change', (v) => setScrolled(v > 24))
  return (
    <header className={`nav${scrolled ? ' nav--scrolled' : ''}`}>
      <div className="container nav__inner">
        <a className="brand" href="#top">
          <img src="/logo.png" alt="" width="32" height="32" />
          SmoothCam
        </a>
        <nav className="nav__links" aria-label="Sections">
          <a href="#demo">Demo</a>
          <a href="#features">Features</a>
          <a href="#how">How it works</a>
          <a href="#upscalers">Upscalers</a>
          <a href="#faq">FAQ</a>
        </nav>
        <a className="btn btn--primary btn--sm" href={release.url}>
          <Download size={16} /> Download
        </a>
      </div>
    </header>
  )
}

const word = {
  hidden: { opacity: 0, y: 48, filter: 'blur(10px)' },
  show: { opacity: 1, y: 0, filter: 'blur(0px)', transition: { duration: 0.9, ease } },
}

function Hero({ release }) {
  const ref = useRef(null)
  const inView = useInView(ref)
  const { scrollYProgress } = useScroll({ target: ref, offset: ['start start', 'end start'] })
  const y = useTransform(scrollYProgress, [0, 1], [0, 140])
  const opacity = useTransform(scrollYProgress, [0, 0.75], [1, 0])

  return (
    <section id="top" className="hero" ref={ref}>
      <div className="hero__canvas" aria-hidden="true">
        {hasWebGL ? (
          <Suspense fallback={<div className="hero__fallback" />}>
            <Hero3D active={inView} />
          </Suspense>
        ) : (
          <div className="hero__fallback" />
        )}
      </div>
      <div className="hero__scrim" aria-hidden="true" />

      <motion.div className="container hero__content" style={{ y, opacity }}>
        <motion.span className="pill" initial={{ opacity: 0, y: 16 }} animate={{ opacity: 1, y: 0 }} transition={{ duration: 0.7, ease }}>
          <span className="pill__dot" />
          {release.version ? `v${release.version} is out` : 'New'} · Free &amp; open source
        </motion.span>

        <motion.h1
          className="hero__title"
          initial="hidden"
          animate="show"
          variants={{ show: { transition: { staggerChildren: 0.09, delayChildren: 0.1 } } }}
        >
          <motion.span className="word" variants={word}>Your&nbsp;</motion.span>
          <motion.span className="word" variants={word}>webcam,</motion.span>
          <br />
          <motion.span className="word gradient-text" variants={word}>at 60 fps.</motion.span>
        </motion.h1>

        <motion.p className="hero__lead" initial={{ opacity: 0, y: 20 }} animate={{ opacity: 1, y: 0 }} transition={{ duration: 0.8, delay: 0.45, ease }}>
          SmoothCam fills in the frames your camera never captured, upscales to 4K, cleans up noise,
          brightens dark rooms, blurs your background and keeps you framed. Everything runs on your own PC.
        </motion.p>

        <motion.div className="hero__ctas" initial={{ opacity: 0, y: 20 }} animate={{ opacity: 1, y: 0 }} transition={{ duration: 0.8, delay: 0.6, ease }}>
          <a className="btn btn--primary btn--lg" href={release.url}>
            <Download size={18} /> Download for Windows
          </a>
          <a className="btn btn--ghost btn--lg" href="#demo">
            <Play size={18} /> See the difference
          </a>
        </motion.div>

        <motion.p className="hero__fine" initial={{ opacity: 0 }} animate={{ opacity: 1 }} transition={{ duration: 0.8, delay: 0.8 }}>
          {release.version ? `Version ${release.version} · ${release.sizeMb} MB` : 'Latest version'} · Windows 11 · No account, no telemetry
        </motion.p>
      </motion.div>

      <div className="hero__scroll" aria-hidden="true"><span /></div>
    </section>
  )
}

function AppsMarquee() {
  return (
    <div className="marquee" aria-label={`Choose SmoothCam as your camera in ${APPS.join(', ')}`}>
      <div className="marquee__track" aria-hidden="true">
        {[...APPS, ...APPS, ...APPS, ...APPS].map((app, i) => (
          <span key={i}>{app}<i>✦</i></span>
        ))}
      </div>
    </div>
  )
}

// The same motion, choppy (quantised to camera frames) or smooth.
const smooth = (t) => (t < 0.5 ? 2 * t * t : 1 - Math.pow(-2 * t + 2, 2) / 2)
const choppy = (t) => Math.floor(smooth(t) * 14) / 14

function Demo() {
  const [on, setOn] = useState(true)
  return (
    <section id="demo" className="section section--glow">
      <div className="container">
        <SectionHead
          center
          eyebrow="Feel the difference"
          title={<>Choppy in. <span className="gradient-text">Smooth out.</span></>}
          sub="Flip the switch, just like in the app. Same motion, with and without the frames SmoothCam fills in."
        />
        <Reveal delay={0.1}>
          <div className="demo">
            <div className="demo__bar">
              <div>
                <div className="demo__label">{on ? 'SmoothCam on' : 'SmoothCam off'}</div>
                <div className="demo__fps">{on ? '60 fps · generated frames filled in' : '30 fps · your webcam as it is'}</div>
              </div>
              <button
                type="button"
                className={`switch${on ? ' is-on' : ''}`}
                role="switch"
                aria-checked={on}
                aria-label="SmoothCam on or off"
                onClick={() => setOn((v) => !v)}
              >
                <span />
              </button>
            </div>

            <div className="demo__stage">
              <div className="demo__grid" />
              <div className="demo__track">
                <motion.div
                  key={on ? 'on' : 'off'}
                  className="demo__ball"
                  animate={{ left: ['0%', '100%'] }}
                  transition={{ duration: 1.8, repeat: Infinity, repeatType: 'mirror', ease: on ? smooth : choppy }}
                />
              </div>
            </div>

            <div className="demo__ticks" aria-hidden="true">
              {Array.from({ length: 24 }, (_, i) => (
                <motion.span
                  key={i}
                  className={`tick ${i % 2 ? 'gen' : 'real'}${on ? ' on' : ''}`}
                  initial={false}
                  animate={{ scaleY: i % 2 && !on ? 0.35 : 1 }}
                  transition={{ duration: 0.4, delay: on ? i * 0.015 : 0 }}
                />
              ))}
            </div>
            <div className="demo__legend">
              <span><i className="legend real" />Camera frames</span>
              <span><i className="legend gen" />Frames SmoothCam generates</span>
            </div>
          </div>
        </Reveal>
      </div>
    </section>
  )
}

function FeatureCard({ icon: Icon, title, text, index }) {
  const onMove = (e) => {
    const r = e.currentTarget.getBoundingClientRect()
    e.currentTarget.style.setProperty('--mx', `${e.clientX - r.left}px`)
    e.currentTarget.style.setProperty('--my', `${e.clientY - r.top}px`)
  }
  return (
    <motion.article
      className="card feature"
      onMouseMove={onMove}
      initial={{ opacity: 0, y: 40 }}
      whileInView={{ opacity: 1, y: 0 }}
      viewport={{ once: true, amount: 0.3 }}
      transition={{ duration: 0.7, delay: (index % 4) * 0.08, ease }}
      whileHover={{ y: -6 }}
    >
      <div className="feature__icon"><Icon size={22} /></div>
      <h3>{title}</h3>
      <p>{text}</p>
    </motion.article>
  )
}

function Features() {
  return (
    <section id="features" className="section">
      <div className="container">
        <SectionHead
          eyebrow="Everything a cheap webcam is missing"
          title="One app. Every upgrade."
          sub="Each feature runs on your graphics card in real time, and one switch turns it all off."
        />
        <div className="features">
          {FEATURES.map((f, i) => <FeatureCard key={f.title} {...f} index={i} />)}
        </div>
      </div>
    </section>
  )
}

function Pipeline() {
  const ref = useRef(null)
  const { scrollYProgress } = useScroll({ target: ref, offset: ['start start', 'end end'] })
  const [active, setActive] = useState(0)
  useMotionValueEvent(scrollYProgress, 'change', (v) =>
    setActive(Math.min(STAGES.length - 1, Math.max(0, Math.floor(v * STAGES.length)))),
  )
  const stage = STAGES[active]

  return (
    <section id="how" className="pipeline" ref={ref} style={{ height: `${STAGES.length * 65 + 40}vh` }}>
      <div className="pipeline__sticky">
        <div className="container pipeline__grid">
          <div>
            <span className="eyebrow">How it works</span>
            <h2 className="h2">From choppy to smooth, in six steps.</h2>
            <ol className="pipeline__list">
              {STAGES.map((s, i) => (
                <li key={s.k} className={i === active ? 'is-active' : i < active ? 'is-done' : ''}>
                  <span className="pipeline__num">{i < active ? <Check size={15} /> : i + 1}</span>
                  <div>
                    <strong>{s.title}</strong>
                    <p>{s.text}</p>
                  </div>
                </li>
              ))}
            </ol>
          </div>
          <div className="pipeline__visual" aria-hidden="true">
            <div className="pipeline__orb" />
            <AnimatePresence mode="wait">
              <motion.div
                key={stage.k}
                className="pipeline__card"
                initial={{ opacity: 0, y: 30, rotateX: -12, scale: 0.95 }}
                animate={{ opacity: 1, y: 0, rotateX: 0, scale: 1 }}
                exit={{ opacity: 0, y: -30, rotateX: 12, scale: 0.95 }}
                transition={{ duration: 0.45, ease }}
              >
                <span className="pipeline__step">Step {active + 1} of {STAGES.length}</span>
                <span className="pipeline__metric gradient-text">{stage.metric}</span>
                <span className="pipeline__k">{stage.k}</span>
              </motion.div>
            </AnimatePresence>
            <div className="pipeline__bar"><motion.span style={{ scaleX: scrollYProgress }} /></div>
          </div>
        </div>
      </div>
    </section>
  )
}

function Counter({ to, suffix = '' }) {
  const ref = useRef(null)
  const inView = useInView(ref, { once: true, amount: 0.6 })
  const [n, setN] = useState(0)
  useEffect(() => {
    if (!inView) return
    const controls = animate(0, to, { duration: 1.6, ease: 'easeOut', onUpdate: (v) => setN(Math.round(v)) })
    return () => controls.stop()
  }, [inView, to])
  return <span ref={ref}>{n}{suffix}</span>
}

function Stats() {
  const stats = [
    { to: 60, suffix: ' fps', label: 'from a 24 or 30 fps webcam' },
    { to: 4, suffix: 'K', label: 'maximum output resolution' },
    { to: 5, suffix: '', label: 'open-source upscalers to choose from' },
    { to: 0, suffix: '', label: 'bytes uploaded. Everything stays on your PC.' },
  ]
  return (
    <section className="section section--tight">
      <div className="container stats">
        {stats.map((s, i) => (
          <Reveal key={s.label} delay={i * 0.08} className="stat">
            <div className="stat__value"><Counter to={s.to} suffix={s.suffix} /></div>
            <div className="stat__label">{s.label}</div>
          </Reveal>
        ))}
      </div>
    </section>
  )
}

function Upscalers() {
  return (
    <section id="upscalers" className="section section--glow">
      <div className="container">
        <SectionHead
          eyebrow="Up to 4K"
          title="Pick your upscaler."
          sub="All of them are open source and run on any DirectX 11 GPU, not only NVIDIA ones. None adds a frame of delay."
        />
        <div className="upscalers">
          {UPSCALERS.map((u, i) => (
            <motion.article
              key={u.name}
              className={`card upscaler${u.best ? ' upscaler--best' : ''}`}
              initial={{ opacity: 0, y: 40, rotateX: 8 }}
              whileInView={{ opacity: 1, y: 0, rotateX: 0 }}
              viewport={{ once: true, amount: 0.3 }}
              transition={{ duration: 0.7, delay: (i % 3) * 0.08, ease }}
              whileHover={{ y: -6 }}
            >
              {u.best && <span className="badge">Default</span>}
              <span className="upscaler__by">{u.by}</span>
              <h3>{u.name}</h3>
              <p>{u.text}</p>
            </motion.article>
          ))}
        </div>
      </div>
    </section>
  )
}

function DownloadSection({ release }) {
  return (
    <section id="download" className="section">
      <div className="container">
        <Reveal>
          <div className="download">
            <div>
              <span className="eyebrow">Download</span>
              <h2 className="h2">Ready before your next call.</h2>
              <p className="sub">Free, MIT-licensed and fully offline. Install, pick “SmoothCam” in your call app, done.</p>
              <div className="download__cta">
                <a className="btn btn--primary btn--lg" href={release.url}>
                  <Download size={18} /> Download for Windows
                </a>
                <a className="btn btn--ghost btn--lg" href={RELEASES}>All releases</a>
              </div>
              <p className="download__meta">
                {release.version ? `Version ${release.version} · ${release.sizeMb} MB · ` : ''}Windows 11 x64 installer
              </p>
              {release.sha256 && (
                <p className="download__hash">SHA-256 <code>{release.sha256}</code></p>
              )}
            </div>
            <ul className="checklist">
              <li><Check size={18} />Windows 11, any DirectX 11 GPU and a webcam</li>
              <li><Check size={18} />Admin rights once, to register the camera for all users</li>
              <li><Check size={18} />Not code-signed yet: if Windows says “Windows protected your PC”, click More info → Run anyway</li>
              <li><Check size={18} />Silent install for IT: <code>/VERYSILENT /SUPPRESSMSGBOXES /NORESTART</code></li>
            </ul>
          </div>
        </Reveal>
      </div>
    </section>
  )
}

function Faq() {
  const [open, setOpen] = useState(0)
  return (
    <section id="faq" className="section">
      <div className="container">
        <SectionHead center eyebrow="FAQ" title="Good questions." />
        <div className="faq">
          {FAQS.map((f, i) => (
            <Reveal key={f.q} delay={i * 0.04} y={20}>
              <div className={`faq__item${open === i ? ' is-open' : ''}`}>
                <button type="button" className="faq__q" aria-expanded={open === i} onClick={() => setOpen(open === i ? -1 : i)}>
                  {f.q}
                  <ChevronDown size={20} />
                </button>
                <AnimatePresence initial={false}>
                  {open === i && (
                    <motion.div
                      initial={{ height: 0, opacity: 0 }}
                      animate={{ height: 'auto', opacity: 1 }}
                      exit={{ height: 0, opacity: 0 }}
                      transition={{ duration: 0.35, ease }}
                    >
                      <p className="faq__a">{f.a}</p>
                    </motion.div>
                  )}
                </AnimatePresence>
              </div>
            </Reveal>
          ))}
        </div>
      </div>
    </section>
  )
}

function Privacy() {
  const items = [
    { icon: Lock, title: 'Video stays on your PC', text: 'Frames are processed in memory and handed to the app you chose. Nothing is saved, recorded or sent anywhere.' },
    { icon: EyeOff, title: 'No face recognition', text: 'Auto-framing only finds where a face is, and blur only finds a person’s outline. Neither identifies anyone.' },
    { icon: ShieldCheck, title: 'No tracking', text: 'No telemetry, analytics, ads, crash uploads or account. SmoothCam contains no network code at all.' },
    { icon: HardDrive, title: 'Local files only', text: 'Settings and logs live in C:\\ProgramData\\SmoothCam. Logs never contain images.' },
  ]
  return (
    <section id="privacy" className="section section--glow">
      <div className="container">
        <SectionHead eyebrow="Privacy" title="Your face never leaves your PC." />
        <div className="features">
          {items.map((it, i) => <FeatureCard key={it.title} {...it} index={i} />)}
        </div>
      </div>
    </section>
  )
}

function Footer({ release }) {
  return (
    <footer className="footer">
      <div className="container">
        <div className="footer__top">
          <a className="brand" href="#top">
            <img src="/logo.png" alt="" width="28" height="28" />
            SmoothCam
          </a>
          <nav className="footer__links" aria-label="Links">
            <a href={release.url}>Download</a>
            <a href={RELEASES}>Release notes</a>
            <a href={REPO}>Source code</a>
            <a href={`${REPO}/issues`}>Report a problem</a>
            <a href="#privacy">Privacy</a>
          </nav>
        </div>
        <p>
          MIT License. Built with AMD FidelityFX FSR 1 and CAS, NVIDIA Image Scaling and Optical Flow, Google’s MediaPipe
          selfie segmentation, Microsoft WIL and Inno Setup.
        </p>
        <p>
          NVIDIA, AMD, FidelityFX, Google, MediaPipe, Windows, Microsoft Teams and Zoom are trademarks of their respective
          owners. SmoothCam is an independent project and isn’t affiliated with or endorsed by any of them.
        </p>
      </div>
    </footer>
  )
}

export default function App() {
  const release = useLatestRelease()
  return (
    <>
      <ScrollProgress />
      <Nav release={release} />
      <main>
        <Hero release={release} />
        <AppsMarquee />
        <Demo />
        <Features />
        <Pipeline />
        <Stats />
        <Upscalers />
        <DownloadSection release={release} />
        <Faq />
        <Privacy />
      </main>
      <Footer release={release} />
    </>
  )
}
