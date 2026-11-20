import { useEffect } from 'react'
import { MainScreen } from './MainScreen'
import { screenTitles, useScreen, type ScreenId } from './screens'
import { TopBar } from './TopBar'
import { VideoScreen } from './VideoScreen'

const screens: Record<ScreenId, () => React.JSX.Element> = {
  main: MainScreen,
  video: VideoScreen
}

/** One window: the shared top bar plus the layout of the screen this window shows. */
export function AppShell(): React.JSX.Element {
  const screen = useScreen()
  const Screen = screens[screen]

  useEffect(() => {
    document.title =
      screen === 'main' ? 'Legacy Robotics' : `Legacy Robotics · ${screenTitles[screen]}`
  }, [screen])

  return (
    <div className="flex h-full flex-col">
      <TopBar screen={screen} />
      <Screen />
    </div>
  )
}
