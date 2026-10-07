import { useRef } from 'react'
import { Canvas, useFrame, useThree } from '@react-three/fiber'
import { Environment, Float, Lightformer, RoundedBox, Sparkles } from '@react-three/drei'
import { Bloom, EffectComposer } from '@react-three/postprocessing'
import * as THREE from 'three'

const BG = '#07070b'
const reduceMotion =
  typeof window !== 'undefined' && window.matchMedia('(prefers-reduced-motion: reduce)').matches
// Phones and low-end PCs get a lighter scene: lower resolution, fewer
// particles and frames, no bloom pass.
const lowPower =
  typeof navigator !== 'undefined' &&
  ((navigator.hardwareConcurrency || 8) <= 4 ||
    (navigator.deviceMemory || 8) <= 4 ||
    window.matchMedia('(max-width: 640px)').matches)

// Wide screens: webcam on the right of the headline. Tall screens: above it.
function useLayout() {
  const { viewport } = useThree()
  const wide = viewport.aspect > 1.15
  return {
    x: wide ? viewport.width * 0.23 : 0,
    y: wide ? 0 : viewport.height * 0.2,
    scale: wide ? Math.min(1, viewport.width / 11) : Math.min(0.62, viewport.width / 4.4),
    viewport,
  }
}

// A stylised external webcam: rounded body, glass lens, glowing rim and a
// green "on" light - the same green as the app's switch.
function Webcam() {
  const group = useRef()
  const rim = useRef()
  const layout = useLayout()

  useFrame((state, dt) => {
    const g = group.current
    if (!g) return
    const scroll = Math.min(window.scrollY / window.innerHeight, 1.2)
    const px = reduceMotion ? 0 : state.pointer.x
    const py = reduceMotion ? 0 : state.pointer.y
    g.rotation.y = THREE.MathUtils.damp(g.rotation.y, -0.42 + px * 0.35 + scroll * 0.9, 3, dt)
    g.rotation.x = THREE.MathUtils.damp(g.rotation.x, 0.1 - py * 0.22 + scroll * 0.3, 3, dt)
    g.position.y = THREE.MathUtils.damp(g.position.y, layout.y + scroll * 1.6, 3, dt)
    if (rim.current && !reduceMotion) rim.current.rotation.z += dt * 0.6
  })

  return (
    <group ref={group} position={[layout.x, layout.y, 0]} scale={layout.scale}>
      {/* body */}
      <RoundedBox args={[3.4, 1.5, 1.1]} radius={0.5} smoothness={8}>
        <meshPhysicalMaterial color="#16161f" metalness={0.75} roughness={0.28} clearcoat={1} clearcoatRoughness={0.12} />
      </RoundedBox>
      {/* lens barrel */}
      <mesh position={[0, 0, 0.62]} rotation={[Math.PI / 2, 0, 0]}>
        <cylinderGeometry args={[0.64, 0.68, 0.34, 72]} />
        <meshStandardMaterial color="#0b0b12" metalness={1} roughness={0.22} />
      </mesh>
      {/* glowing rim */}
      <mesh ref={rim} position={[0, 0, 0.8]}>
        <torusGeometry args={[0.64, 0.045, 24, 160, Math.PI * 1.7]} />
        <meshStandardMaterial color="#7b5cff" emissive="#7b5cff" emissiveIntensity={3} toneMapped={false} />
      </mesh>
      {/* iris */}
      <mesh position={[0, 0, 0.8]}>
        <ringGeometry args={[0.3, 0.5, 72]} />
        <meshStandardMaterial color="#2a7dff" emissive="#2a6cff" emissiveIntensity={0.9} metalness={0.3} roughness={0.25} />
      </mesh>
      <mesh position={[0, 0, 0.805]}>
        <circleGeometry args={[0.3, 64]} />
        <meshStandardMaterial color="#030308" roughness={0.2} metalness={0.4} />
      </mesh>
      <mesh position={[-0.11, 0.11, 0.83]}>
        <circleGeometry args={[0.06, 32]} />
        <meshBasicMaterial color="#ffffff" toneMapped={false} />
      </mesh>
      {/* glass dome */}
      <mesh position={[0, 0, 0.79]} rotation={[Math.PI / 2, 0, 0]} scale={[1, 0.45, 1]}>
        <sphereGeometry args={[0.6, 64, 32, 0, Math.PI * 2, 0, Math.PI / 2]} />
        <meshPhysicalMaterial transmission={1} thickness={0.5} roughness={0} ior={1.5} color="#cfe1ff" />
      </mesh>
      {/* status light */}
      <mesh position={[1.12, 0.42, 0.53]}>
        <sphereGeometry args={[0.055, 24, 24]} />
        <meshStandardMaterial color="#3ddc84" emissive="#3ddc84" emissiveIntensity={6} toneMapped={false} />
      </mesh>
      {/* stand */}
      <mesh position={[0, -0.98, -0.05]}>
        <cylinderGeometry args={[0.09, 0.12, 0.5, 24]} />
        <meshStandardMaterial color="#1b1b25" metalness={0.8} roughness={0.3} />
      </mesh>
      <mesh position={[0, -1.25, -0.05]}>
        <cylinderGeometry args={[0.75, 0.8, 0.08, 64]} />
        <meshStandardMaterial color="#14141c" metalness={0.8} roughness={0.35} />
      </mesh>
    </group>
  )
}

// Video frames streaming past the camera: on its left only the real frames
// (choppy), on its right the generated ones light up in between (smooth).
function FrameStream({ count = lowPower ? 16 : 28 }) {
  const frames = useRef([])
  const layout = useLayout()
  const span = layout.viewport.width * 1.7 + 4
  const spacing = span / count

  useFrame((state) => {
    const t = reduceMotion ? 0 : state.clock.elapsedTime
    frames.current.forEach((mesh, i) => {
      if (!mesh) return
      const x = ((i * spacing + t * 0.8) % span) - span / 2
      mesh.position.x = x
      mesh.position.y = Math.sin(x * 0.35 + 1) * 0.25
      const past = THREE.MathUtils.smoothstep(x, layout.x - 0.3, layout.x + 0.9)
      // Only near the webcam, so the stream never runs behind the headline.
      const near = THREE.MathUtils.smoothstep(x, layout.x - 2.4, layout.x - 1.4)
      mesh.material.opacity = near * (i % 2 ? past * 0.55 : 0.14 + past * 0.12)
    })
  })

  return (
    <group position={[0, layout.y - 0.05, -2.4]}>
      {Array.from({ length: count }, (_, i) => (
        <mesh key={i} ref={(el) => (frames.current[i] = el)}>
          <planeGeometry args={[0.95, 0.56]} />
          <meshBasicMaterial
            color={i % 2 ? '#3ddc84' : '#a9b8ff'}
            transparent
            opacity={0.15}
            depthWrite={false}
            toneMapped={false}
          />
        </mesh>
      ))}
    </group>
  )
}

export default function Hero3D({ active = true }) {
  return (
    <Canvas
      frameloop={active ? 'always' : 'never'}
      dpr={lowPower ? [1, 1.25] : [1, 1.75]}
      camera={{ position: [0, 0, 8], fov: 40 }}
      gl={{ antialias: !lowPower, powerPreference: 'high-performance' }}
      eventSource={document.getElementById('root')}
      eventPrefix="client"
    >
      <color attach="background" args={[BG]} />
      <fog attach="fog" args={[BG, 9, 17]} />
      <ambientLight intensity={0.35} />
      <directionalLight position={[3, 4, 5]} intensity={1.3} />
      <pointLight position={[-4, -2, 3]} intensity={25} color="#7b5cff" />
      <pointLight position={[4, 2, 2]} intensity={15} color="#3aa0ff" />

      <FrameStream />
      <Float speed={reduceMotion ? 0 : 1.4} rotationIntensity={0.18} floatIntensity={0.5}>
        <Webcam />
      </Float>
      <Sparkles count={lowPower ? 35 : 80} scale={[16, 8, 6]} size={2.4} speed={reduceMotion ? 0 : 0.35} opacity={0.55} color="#a99bff" />

      <Environment resolution={lowPower ? 128 : 256}>
        <group rotation={[-Math.PI / 3, 0, 1]}>
          <Lightformer form="circle" intensity={4} rotation-x={Math.PI / 2} position={[0, 5, -9]} scale={2} />
          <Lightformer form="circle" intensity={2} rotation-y={Math.PI / 2} position={[-5, 1, -1]} scale={2} />
          <Lightformer form="ring" color="#7b5cff" intensity={8} rotation-y={Math.PI / 2} position={[-0.1, -1, -5]} scale={10} />
          <Lightformer form="rect" color="#3aa0ff" intensity={4} rotation-y={-Math.PI / 2} position={[10, 1, 0]} scale={[10, 2, 1]} />
        </group>
      </Environment>

      {!lowPower && (
        <EffectComposer>
          <Bloom mipmapBlur luminanceThreshold={0.9} intensity={0.9} />
        </EffectComposer>
      )}
    </Canvas>
  )
}
