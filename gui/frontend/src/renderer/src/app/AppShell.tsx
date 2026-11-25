import { useEffect } from 'react'
import { MainScreen } from './MainScreen'
import { screenTitle, useScreen, type ScreenId, type ScreenView } from './screens'
import { TopBar } from './TopBar'
import { VideoScreen } from './VideoScreen'

const screens: Record<ScreenId, (props: { view?: string }) => React.JSX.Element> = {
  main: MainScreen,
  video: VideoScreen
}

/** One window: the shared top bar plus the layout of the screen this window shows. */
export function AppShell(): React.JSX.Element {
  const current: ScreenView = useScreen()
  const Screen = screens[current.screen]
  const title = current.screen === 'main' ? undefined : screenTitle(current)

  useEffect(() => {
    document.title = title ? `Legacy Robotics · ${title}` : 'Legacy Robotics'
  }, [title])

  return (
    <div className="flex h-full flex-col">
      <TopBar current={current} />
      <Screen view={current.view} />
    </div>
  )
}
