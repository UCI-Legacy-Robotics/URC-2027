import { app, shell, BrowserWindow, ipcMain } from 'electron'
import { join } from 'path'
import { electronApp, optimizer, is } from '@electron-toolkit/utils'
import icon from '../../resources/icon.png?asset'

const isMac = process.platform === 'darwin'

/** Screens the UI can show, each in its own window. Keep in sync with src/renderer/src/app/screens.ts. */
const screens = ['main', 'video'] as const
type Screen = (typeof screens)[number]

const isScreen = (value: unknown): value is Screen =>
  typeof value === 'string' && (screens as readonly string[]).includes(value)

/**
 * The main window, at most one. Other screens can have any number of windows (one per
 * monitor), each choosing its own tiles.
 */
let mainWindow: BrowserWindow | undefined
const otherWindows = new Set<BrowserWindow>()

function createWindow(screen: Screen): BrowserWindow {
  const win = new BrowserWindow({
    width: 1440,
    height: 900,
    show: false,
    backgroundColor: '#0d0d0d',
    autoHideMenuBar: true,
    // No OS title bar: the UI's top bar is the title bar, with its own window buttons.
    // macOS keeps its native buttons, drawn over the left end of the top bar.
    ...(isMac ? { titleBarStyle: 'hidden' as const } : { frame: false }),
    ...(process.platform === 'linux' ? { icon } : {}),
    webPreferences: {
      preload: join(__dirname, '../preload/index.js'),
      sandbox: true
    }
  })

  if (screen === 'main') mainWindow = win
  else otherWindows.add(win)
  win.on('closed', () => {
    if (screen !== 'main') {
      otherWindows.delete(win)
      return
    }
    mainWindow = undefined
    // The main window owns the session: closing it closes the other screens too.
    for (const other of otherWindows) other.close()
  })

  win.on('ready-to-show', () => {
    win.show()
  })

  // Tell the UI so the maximize button can switch to "restore".
  const sendMaximized = (): void => win.webContents.send('window:maximized', win.isMaximized())
  win.on('maximize', sendMaximized)
  win.on('unmaximize', sendMaximized)

  win.webContents.setWindowOpenHandler((details) => {
    shell.openExternal(details.url)
    return { action: 'deny' }
  })

  // The URL hash tells the UI which screen to show (no hash = main).
  // HMR for renderer base on electron-vite cli.
  // Load the remote URL for development or the local html file for production.
  const hash = screen === 'main' ? '' : `/${screen}`
  if (is.dev && process.env['ELECTRON_RENDERER_URL']) {
    win.loadURL(`${process.env['ELECTRON_RENDERER_URL']}${hash ? `#${hash}` : ''}`)
  } else {
    win.loadFile(join(__dirname, '../renderer/index.html'), { hash })
  }

  return win
}

/** Focuses the main window if it's open; opens a new window for anything else. */
function openScreen(screen: Screen): void {
  if (screen === 'main' && mainWindow) {
    if (mainWindow.isMinimized()) mainWindow.restore()
    mainWindow.focus()
  } else {
    createWindow(screen)
  }
}

// Window buttons in the UI's top bar. Each acts on the window that sent it.
function registerWindowControls(): void {
  const senderWindow = (event: Electron.IpcMainEvent): BrowserWindow | null =>
    BrowserWindow.fromWebContents(event.sender)

  ipcMain.on('window:minimize', (event) => senderWindow(event)?.minimize())
  ipcMain.on('window:toggle-maximize', (event) => {
    const win = senderWindow(event)
    if (!win) return
    if (win.isMaximized()) win.unmaximize()
    else win.maximize()
  })
  ipcMain.on('window:close', (event) => senderWindow(event)?.close())
  ipcMain.on('window:open-screen', (_event, screen: unknown) => {
    if (isScreen(screen)) openScreen(screen)
  })
}

app.whenReady().then(() => {
  // Set app user model id for windows
  electronApp.setAppUserModelId('edu.uci.legacyrobotics.basestation')

  // Default open or close DevTools by F12 in development
  // and ignore CommandOrControl + R in production.
  // see https://github.com/alex8088/electron-toolkit/tree/master/packages/utils
  app.on('browser-window-created', (_, window) => {
    optimizer.watchWindowShortcuts(window)
  })

  registerWindowControls()
  openScreen('main')

  app.on('activate', function () {
    // On macOS it's common to re-create a window in the app when the
    // dock icon is clicked and there are no other windows open.
    if (BrowserWindow.getAllWindows().length === 0) openScreen('main')
  })
})

// Quit when all windows are closed, except on macOS. There, it's common
// for applications and their menu bar to stay active until the user quits
// explicitly with Cmd + Q.
app.on('window-all-closed', () => {
  if (!isMac) {
    app.quit()
  }
})
