/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2025 Chris Rizzitello <sithlord48@gmail.com>
 * SPDX-FileCopyrightText: (C) 2012 - 2016 Synergy App Ltd
 * SPDX-FileCopyrightText: (C) 2008 Volker Lanz <vl@fidra.de>
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "ServerConfigDialog.h"
#include "ui_ServerConfigDialog.h"

#include "common/Constants.h"
#include "common/NetworkProtocol.h"
#include "common/PlatformInfo.h"
#include "common/Settings.h"
#include "dialogs/ActionDialog.h"
#include "dialogs/HotkeyDialog.h"
#include "dialogs/ScreenSettingsDialog.h"

#include <QFileDialog>
#include <QGuiApplication>
#include <QMessageBox>
#include <QScreen>

#include <algorithm>

using enum ScreenConfig::SwitchCorner;

namespace {

using ReportedMonitor = deskflow::gui::CoreProcess::ReportedMonitor;

// Lays out one machine's monitors on the canvas, each at its own true
// physical size (mm), keeping the OS arrangement between them: monitors that
// share an edge in pixel space share it on the canvas too, aligned the same
// way (top, bottom or centre). Each monitor's real pixel rect is kept so link
// fractions can be mapped back per monitor (see ServerConfig::computeCanvasLinks).
// The monitor at the pixel origin (the primary one) sits at canvas (0, 0).
QList<gui::canvas::MonitorRect> layoutMonitors(const QList<ReportedMonitor> &reported)
{
  if (reported.isEmpty()) {
    return {};
  }

  constexpr qreal kAssumedDpi = 96.0;
  constexpr qreal kMmPerInch = 25.4;

  // mm per pixel, per monitor; monitors without a usable physical size
  // borrow the average of those that have one (or an assumed 96 DPI).
  QList<qreal> scales;
  qreal scaleSum = 0;
  int scaleCount = 0;
  for (const auto &monitor : reported) {
    const bool known = monitor.mmSize.width() > 0 && monitor.rect.width() > 0;
    scales.append(known ? static_cast<qreal>(monitor.mmSize.width()) / monitor.rect.width() : 0.0);
    if (known) {
      scaleSum += scales.last();
      ++scaleCount;
    }
  }
  const qreal fallbackScale = scaleCount > 0 ? scaleSum / scaleCount : (kMmPerInch / kAssumedDpi);
  for (auto &scale : scales) {
    if (scale <= 0) {
      scale = fallbackScale;
    }
  }

  const int count = static_cast<int>(reported.size());
  QList<QRectF> rects;
  for (int i = 0; i < count; ++i) {
    rects.append(QRectF(QPointF(), QSizeF(reported[i].rect.size()) * scales[i]));
  }

  int anchor = 0;
  for (int i = 0; i < count; ++i) {
    if (reported[i].rect.contains(QPoint(0, 0))) {
      anchor = i;
      break;
    }
  }

  // Perpendicular offset of n relative to placed m, given the pixel-space
  // starts/ends of both along that axis.
  auto alignedStart = [](qreal mStart, qreal mLen, qreal nLen, int mPxStart, int mPxEnd, int nPxStart, int nPxEnd,
                         qreal mScale) {
    if (nPxStart == mPxStart)
      return mStart;
    if (nPxEnd == mPxEnd)
      return mStart + mLen - nLen;
    if (nPxStart + nPxEnd == mPxStart + mPxEnd)
      return mStart + (mLen - nLen) / 2;
    return mStart + (nPxStart - mPxStart) * mScale;
  };

  QList<bool> placed(count, false);
  placed[anchor] = true;
  QList<int> queue{anchor};
  while (!queue.isEmpty()) {
    const int m = queue.takeFirst();
    const QRect mp = reported[m].rect;
    const QRectF &mr = rects[m];
    for (int n = 0; n < count; ++n) {
      if (placed[n])
        continue;
      const QRect np = reported[n].rect;
      const bool overlapY = np.top() <= mp.bottom() && mp.top() <= np.bottom();
      const bool overlapX = np.left() <= mp.right() && mp.left() <= np.right();
      const qreal nw = rects[n].width();
      const qreal nh = rects[n].height();
      // QRect::right()/bottom() are inclusive, so x() + width() is the edge.
      const int mpRight = mp.x() + mp.width();
      const int mpBottom = mp.y() + mp.height();
      const int npRight = np.x() + np.width();
      const int npBottom = np.y() + np.height();
      qreal x;
      qreal y;
      if (overlapY && (np.x() == mpRight || npRight == mp.x())) {
        x = np.x() == mpRight ? mr.right() : mr.left() - nw;
        y = alignedStart(mr.top(), mr.height(), nh, mp.y(), mpBottom, np.y(), npBottom, scales[m]);
      } else if (overlapX && (np.y() == mpBottom || npBottom == mp.y())) {
        y = np.y() == mpBottom ? mr.bottom() : mr.top() - nh;
        x = alignedStart(mr.left(), mr.width(), nw, mp.x(), mpRight, np.x(), npRight, scales[m]);
      } else {
        continue;
      }
      rects[n].moveTopLeft(QPointF(x, y));
      placed[n] = true;
      queue.append(n);
    }
  }

  // Monitors not edge-connected to the rest: position linearly relative to
  // the anchor at the anchor's scale.
  const QPoint anchorPx = reported[anchor].rect.topLeft();
  for (int i = 0; i < count; ++i) {
    if (!placed[i]) {
      rects[i].moveTopLeft(QPointF(reported[i].rect.topLeft() - anchorPx) * scales[anchor]);
    }
  }

  QList<gui::canvas::MonitorRect> monitors;
  for (int i = 0; i < count; ++i) {
    const QRect px = reported[i].rect;
    monitors.append(
        gui::canvas::MonitorRect{
            rects[i], QStringLiteral("%1x%2").arg(px.width()).arg(px.height()), px
        }
    );
  }
  return monitors;
}

} // namespace

ServerConfigDialog::ServerConfigDialog(QWidget *parent, ServerConfig &config, deskflow::gui::CoreProcess &coreProcess)
    : QDialog(parent, Qt::WindowTitleHint | Qt::WindowSystemMenuHint),
      ui{std::make_unique<Ui::ServerConfigDialog>()},
      m_originalServerConfig(config),
      m_originalServerConfigIsExternal(config.useExternalConfig()),
      m_originalServerConfigUsesExternalFile(config.configFile()),
      m_serverConfig(config),
      m_canvasScene(m_serverConfig.screens()),
      m_coreProcess(coreProcess)
{
  ui->setupUi(this);

  connect(
      &m_coreProcess, &deskflow::gui::CoreProcess::clientMonitorsChanged, this,
      &ServerConfigDialog::onClientMonitorsChanged
  );

  loadFromConfig();

  ui->btnBrowseConfigFile->setIcon(QIcon::fromTheme(QIcon::ThemeIcon::DocumentOpen));

  // force the first tab, since qt creator sets the active tab as the last one
  // the developer was looking at, and it's easy to accidentally save that.
  ui->tabWidget->setCurrentIndex(0);

  if (!deskflow::platform::isWindows())
    ui->cbWin32KeepForeground->setVisible(false);
  initConnections();
  onChange();
}

ServerConfigDialog::~ServerConfigDialog() = default;

bool ServerConfigDialog::addClient(const QString &clientName)
{
  return addComputer(clientName, true);
}

void ServerConfigDialog::accept()
{
  if (ui->groupExternalConfig->isChecked() && !QFile::exists(ui->lineConfigFile->text())) {

    auto selectedButton = QMessageBox::warning(
        this, "Filename invalid", "Please select a valid configuration file.", QMessageBox::Ok | QMessageBox::Ignore
    );

    if (selectedButton != QMessageBox::Ok || !browseConfigFile()) {
      return;
    }
  }

  if (QStringList errors; !serverConfig().validateCanvasLayout(errors)) {
    QMessageBox::warning(this, tr("Invalid Computer Layout"), errors.join('\n'));
    return;
  }

  // now that the dialog has been accepted, copy the new server config to the
  // original one, which is a reference to the one in MainWindow.
  setOriginalServerConfig(serverConfig());
  Settings::setValue(Settings::Server::Protocol, networkProtocolToOption(m_protocol));
  Settings::setValue(Settings::Server::EnableHeatbeat, m_enableHeartbeat);
  Settings::setValue(Settings::Server::EnableSwitchDelay, m_enableSwitchDelay);
  Settings::setValue(Settings::Server::EnableSwitchDoubleTap, m_enableSwitchDoubleTap);

  QDialog::accept();
}

void ServerConfigDialog::reject()
{
  serverConfig().setUseExternalConfig(m_originalServerConfigIsExternal);
  serverConfig().setConfigFile(m_originalServerConfigUsesExternalFile);

  QDialog::reject();
}

void ServerConfigDialog::addHotkey()
{
  Hotkey hotkey;
  HotkeyDialog dlg(this, hotkey);
  if (dlg.exec() == QDialog::Accepted) {
    serverConfig().hotkeys().append(hotkey);
    ui->listHotkeys->addItem(hotkey.text());
    onChange();
  }
}

void ServerConfigDialog::editHotkey()
{
  int row = ui->listHotkeys->currentRow();
  if (row < 0 || row >= serverConfig().hotkeys().size()) {
    qDebug() << "Attempt to editing out of bounds hotkey row: " << row;
    return;
  }

  Hotkey &hotkey = serverConfig().hotkeys()[row];
  HotkeyDialog dlg(this, hotkey);
  if (dlg.exec() == QDialog::Accepted) {
    ui->listHotkeys->currentItem()->setText(hotkey.text());
    onChange();
  }
}

void ServerConfigDialog::removeHotkey()
{
  int row = ui->listHotkeys->currentRow();
  if (row < 0 || row >= serverConfig().hotkeys().size()) {
    qDebug() << "Attempt to remove out of bounds hotkey row: " << row;
    return;
  }

  serverConfig().hotkeys().removeAt(row);
  ui->listActions->clear();
  delete ui->listHotkeys->item(row);
  onChange();
}

void ServerConfigDialog::listHotkeysSelectionChanged(const QItemSelection &selected, const QItemSelection &)
{
  bool itemsSelected = !selected.isEmpty();
  ui->btnEditHotkey->setEnabled(itemsSelected);
  ui->btnRemoveHotkey->setEnabled(itemsSelected);
  ui->btnNewAction->setEnabled(itemsSelected);

  if (itemsSelected && !serverConfig().hotkeys().isEmpty()) {
    ui->listActions->clear();
    const Hotkey &hotkey = serverConfig().hotkeys().at(selected.indexes().first().row());
    for (const Action &action : hotkey.actions())
      ui->listActions->addItem(action.text());
  }
}

void ServerConfigDialog::addAction()
{
  int row = ui->listHotkeys->currentRow();
  if (row < 0 || row >= serverConfig().hotkeys().size()) {
    qDebug() << "Attempt to add action to out of bounds hotkey row: " << row;
    return;
  }

  Hotkey &hotkey = serverConfig().hotkeys()[row];
  Action action;
  ActionDialog dlg(this, serverConfig(), hotkey, action);
  if (dlg.exec() == QDialog::Accepted) {
    hotkey.actions().append(action);
    ui->listActions->addItem(action.text());
    onChange();
  }
}

void ServerConfigDialog::editAction()
{
  int hotkeyRow = ui->listHotkeys->currentRow();
  if (hotkeyRow < 0 || hotkeyRow >= serverConfig().hotkeys().size()) {
    qDebug() << "Attempt to edit action from out of bounds hotkey row: " << hotkeyRow;
    return;
  }
  Hotkey &hotkey = serverConfig().hotkeys()[hotkeyRow];

  int actionRow = ui->listActions->currentRow();
  if (actionRow < 0 || actionRow >= hotkey.actions().size()) {
    qDebug() << "Attempt to remove out of bounds action row: " << actionRow;
    return;
  }
  Action &action = hotkey.actions()[actionRow];

  ActionDialog dlg(this, serverConfig(), hotkey, action);
  if (dlg.exec() == QDialog::Accepted) {
    ui->listActions->currentItem()->setText(action.text());
    onChange();
  }
}

void ServerConfigDialog::removeAction()
{
  int hotkeyRow = ui->listHotkeys->currentRow();
  if (hotkeyRow < 0 || hotkeyRow >= serverConfig().hotkeys().size()) {
    qDebug() << "Attempt to remove action from out of bounds hotkey row: " << hotkeyRow;
    return;
  }
  Hotkey &hotkey = serverConfig().hotkeys()[hotkeyRow];

  int actionRow = ui->listActions->currentRow();
  if (actionRow < 0 || actionRow >= hotkey.actions().size()) {
    qDebug() << "Attempt to remove out of bounds action row: " << actionRow;
    return;
  }

  hotkey.actions().removeAt(actionRow);
  delete ui->listActions->currentItem();
  onChange();
}

void ServerConfigDialog::toggleClipboard(bool enabled)
{
  ui->sbClipboardSizeLimit->setEnabled(enabled);
  if (enabled && !ui->sbClipboardSizeLimit->value()) {
    auto size = static_cast<int>((ServerConfig::defaultClipboardSharingSize() + 512) / 1024);
    ui->sbClipboardSizeLimit->setValue(size ? size : 1);
  }
  serverConfig().setClipboardSharing(enabled);
  onChange();
}

void ServerConfigDialog::setClipboardLimit(int limit)
{
  serverConfig().setClipboardSharingSize(limit * 1024);
  onChange();
}

void ServerConfigDialog::toggleHeartbeat(bool enabled)
{
  m_enableHeartbeat = enabled;
  ui->sbHeartbeat->setEnabled(enabled);
  onChange();
}

void ServerConfigDialog::setHeartbeat(int rate)
{
  serverConfig().setHeartbeat(rate);
  onChange();
}

void ServerConfigDialog::toggleRelativeMouseMoves(bool enabled)
{
  serverConfig().setRelativeMouseMoves(enabled);
  onChange();
}

void ServerConfigDialog::toggleProtocol()
{
  m_protocol = ui->rbProtocolBarrier->isChecked() ? NetworkProtocol::Barrier : NetworkProtocol::Synergy;
  onChange();
}

void ServerConfigDialog::setSwitchCornerSize(int size)
{
  serverConfig().setSwitchCornerSize(size);
  onChange();
}

void ServerConfigDialog::toggleCornerBottomLeft(bool enable)
{
  serverConfig().setSwitchCorner(static_cast<int>(BottomLeft), enable);
  onChange();
}

void ServerConfigDialog::toggleCornerTopLeft(bool enable)
{
  serverConfig().setSwitchCorner(static_cast<int>(TopLeft), enable);
  onChange();
}

void ServerConfigDialog::toggleCornerBottomRight(bool enable)
{
  serverConfig().setSwitchCorner(static_cast<int>(BottomRight), enable);
  onChange();
}

void ServerConfigDialog::toggleCornerTopRight(bool enable)
{
  serverConfig().setSwitchCorner(static_cast<int>(TopRight), enable);
  onChange();
}

void ServerConfigDialog::listActionsSelectionChanged(const QItemSelection &selected, const QItemSelection &)
{
  bool enabled = !selected.isEmpty();
  ui->btnEditAction->setEnabled(enabled);
  ui->btnRemoveAction->setEnabled(enabled);
}

void ServerConfigDialog::toggleSwitchDoubleTap(bool enable)
{
  m_enableSwitchDoubleTap = enable;
  ui->sbSwitchDoubleTap->setEnabled(enable);
  onChange();
}

void ServerConfigDialog::setSwitchDoubleTap(int within)
{
  serverConfig().setSwitchDoubleTap(within);
  onChange();
}

void ServerConfigDialog::toggleSwitchDelay(bool enable)
{
  m_enableSwitchDelay = enable;
  ui->sbSwitchDelay->setEnabled(enable);
  onChange();
}

void ServerConfigDialog::setSwitchDelay(int delay)
{
  serverConfig().setSwitchDelay(delay);
  onChange();
}

void ServerConfigDialog::toggleDefaultLockToScreenState(bool state)
{
  serverConfig().setDefaultLockToScreenState(state);
  onChange();
}

void ServerConfigDialog::toggleLockToScreen(bool disabled)
{
  serverConfig().setDisableLockToScreen(disabled);
  onChange();
}

void ServerConfigDialog::toggleWin32Foreground(bool enabled)
{
  serverConfig().setWin32KeepForeground(enabled);
  onChange();
}

void ServerConfigDialog::addClient()
{
  addComputer("", false);
}

void ServerConfigDialog::removeSelectedComputer()
{
  canvasScene().removeSelected();
}

void ServerConfigDialog::onScreensChanged()
{
  onChange();
}

void ServerConfigDialog::onClientMonitorsChanged(
    const QString &name, const QList<deskflow::gui::CoreProcess::ReportedMonitor> &monitors
)
{
  Screen *screen = canvasScene().findScreen(name);
  if (!screen || screen->isServer() || !screen->monitors().isEmpty()) {
    // Only auto-populate a machine that has no layout yet (e.g. it just
    // connected for the first time this session) — never silently
    // recompute one that's already arranged.
    return;
  }
  applyLiveMonitors(*screen, monitors);
  m_canvasScene.rebuildFromScreens();
  ui->screenCanvasView->fitContents();
}

void ServerConfigDialog::toggleExternalConfig(bool checked)
{
  ui->widgetExternalConfigControls->setEnabled(checked);
  ui->tabWidget->setTabEnabled(0, !checked);
  ui->tabWidget->setTabEnabled(1, !checked);
  ui->groupMisc->setEnabled(!checked);
  ui->groupCorners->setEnabled(!checked);
  ui->groupSwitch->setEnabled(!checked);
  ui->widgetHeartbeat->setEnabled(!checked);
  serverConfig().setUseExternalConfig(checked);
  onChange();
}

bool ServerConfigDialog::browseConfigFile()
{
  //: %1 is replaced with the application names
  //: (*.conf) and (*.*) should not be translated
  const auto deskflowConfigFilter = tr("%1 Configurations (*.conf);;All files (*.*)");

  QString fileName =
      QFileDialog::getOpenFileName(this, tr("Browse for a config file"), "", deskflowConfigFilter.arg(kAppName));

  if (!fileName.isEmpty()) {
    ui->lineConfigFile->setText(fileName);
    serverConfig().setConfigFile(ui->lineConfigFile->text());
    onChange();
    return true;
  }

  return false;
}

void ServerConfigDialog::loadFromConfig()
{
  m_protocol = Settings::networkProtocol();
  ui->rbProtocolSynergy->setChecked(m_protocol == NetworkProtocol::Synergy);
  ui->rbProtocolBarrier->setChecked(m_protocol == NetworkProtocol::Barrier);

  ui->lineConfigFile->setText(serverConfig().configFile());

  m_enableHeartbeat = Settings::value(Settings::Server::EnableHeatbeat).toBool();
  ui->cbHeartbeat->setChecked(m_enableHeartbeat);
  ui->sbHeartbeat->setEnabled(ui->cbHeartbeat->isChecked());
  ui->sbHeartbeat->setValue(serverConfig().heartbeat());
  ui->cbRelativeMouseMoves->setChecked(serverConfig().relativeMouseMoves());
  ui->cbWin32KeepForeground->setChecked(serverConfig().win32KeepForeground());

  m_enableSwitchDelay = Settings::value(Settings::Server::EnableSwitchDelay).toBool();
  ui->cbSwitchDelay->setChecked(m_enableSwitchDelay);
  ui->sbSwitchDelay->setValue(serverConfig().switchDelay());
  ui->sbSwitchDelay->setEnabled(ui->cbSwitchDelay->isChecked());

  m_enableSwitchDoubleTap = Settings::value(Settings::Server::EnableSwitchDoubleTap).toBool();
  ui->cbSwitchDoubleTap->setChecked(m_enableSwitchDoubleTap);
  ui->sbSwitchDoubleTap->setValue(serverConfig().switchDoubleTap());
  ui->sbSwitchDoubleTap->setEnabled(ui->cbSwitchDoubleTap->isChecked());

  ui->groupExternalConfig->setChecked(serverConfig().useExternalConfig());

  ui->widgetExternalConfigControls->setEnabled(ui->groupExternalConfig->isChecked());
  toggleExternalConfig(ui->groupExternalConfig->isChecked());

  ui->cbCornerTopLeft->setChecked(serverConfig().switchCorner(static_cast<int>(TopLeft)));
  ui->cbCornerTopRight->setChecked(serverConfig().switchCorner(static_cast<int>(TopRight)));
  ui->cbCornerBottomLeft->setChecked(serverConfig().switchCorner(static_cast<int>(BottomLeft)));
  ui->cbCornerBottomRight->setChecked(serverConfig().switchCorner(static_cast<int>(BottomRight)));
  ui->sbSwitchCornerSize->setValue(serverConfig().switchCornerSize());
  ui->cbDefaultLockToScreenState->setChecked(serverConfig().defaultLockToScreenState());

  ui->cbDisableLockToScreen->setChecked(serverConfig().disableLockToScreen());
  ui->cbEnableClipboard->setChecked(serverConfig().clipboardSharing());

  auto clipboardSharingSizeM = static_cast<int>(serverConfig().clipboardSharingSize() / 1024);
  ui->sbClipboardSizeLimit->setValue(clipboardSharingSizeM);
  ui->sbClipboardSizeLimit->setEnabled(serverConfig().clipboardSharing());

  ui->listHotkeys->clear();
  for (const Hotkey &hotkey : std::as_const(serverConfig().hotkeys()))
    ui->listHotkeys->addItem(hotkey.text());

  auto &screens = serverConfig().screens();
  auto server = std::ranges::find_if(screens, [this](const Screen &screen) {
    return (screen.name() == serverConfig().getServerName());
  });

  if (server == screens.end()) {
    Screen serverScreen(serverConfig().getServerName());
    serverScreen.markAsServer();
    screens.append(serverScreen);
    server = screens.end() - 1;
  } else {
    server->markAsServer();
  }

  // Always refresh the local machine's monitors from real, live geometry —
  // this is the one machine we can query directly (this GUI process runs on
  // it), so real detection takes priority over whatever was previously
  // stored (a legacy-grid migration placeholder, or a stale prior layout).
  //
  // Each monitor is drawn at its own physical size; its real
  // QScreen::geometry() — the same pixel/point space the OS reports at
  // runtime (CGDisplayBounds on macOS, the virtual-screen metrics on
  // Windows) and that Server::mapToFraction()/mapToPixel() use — is kept
  // alongside so links map back to the right pixels on each monitor.
  QList<ReportedMonitor> localReported;
  for (const QScreen *qscreen : QGuiApplication::screens()) {
    const QSizeF mm = qscreen->physicalSize();
    localReported.append(ReportedMonitor{qscreen->geometry(), QSize(qRound(mm.width()), qRound(mm.height()))});
  }
  QList<gui::canvas::MonitorRect> localMonitors = layoutMonitors(localReported);
  if (localMonitors.isEmpty()) {
    localMonitors.append(gui::canvas::MonitorRect{QRectF(0, 0, 480, 320), QString()});
  }
  server->setMonitors(localMonitors);

  // Remote machines: auto-populate from live data ONLY the first time (this
  // screen has no monitors yet). Once a layout exists — whether from a
  // previous auto-population or the user's own dragging — it's the source
  // of truth and must not be silently recomputed on every reopen: the
  // scale/rounding involved in converting live px reports to canvas mm
  // isn't perfectly idempotent, so redoing it each time could visibly drift
  // a layout the user already arranged and saved.
  for (auto &screen : screens) {
    if (screen.isNull() || screen.isServer() || !screen.monitors().isEmpty()) {
      continue;
    }
    const auto liveMonitors = m_coreProcess.clientMonitors(screen.name());
    if (!liveMonitors.isEmpty()) {
      applyLiveMonitors(screen, liveMonitors);
    }
  }

  m_canvasScene.rebuildFromScreens();
  ui->screenCanvasView->setScene(&m_canvasScene);
  ui->screenCanvasView->fitContents();
}

void ServerConfigDialog::initConnections()
{
  connect(ui->buttonBox, &QDialogButtonBox::accepted, this, &ServerConfigDialog::accept);
  connect(ui->buttonBox, &QDialogButtonBox::rejected, this, &ServerConfigDialog::reject);
  connect(ui->btnAddComputer, &QPushButton::clicked, this, [this] { addClient(); });
  connect(ui->btnRemoveComputer, &QPushButton::clicked, this, &ServerConfigDialog::removeSelectedComputer);
  connect(&m_canvasScene, &ScreenCanvasScene::screensChanged, this, &ServerConfigDialog::onScreensChanged);
  connect(ui->btnNewHotkey, &QPushButton::clicked, this, &ServerConfigDialog::addHotkey);
  connect(ui->btnEditHotkey, &QPushButton::clicked, this, &ServerConfigDialog::editHotkey);
  connect(ui->btnRemoveHotkey, &QPushButton::clicked, this, &ServerConfigDialog::removeHotkey);
  connect(ui->listHotkeys, &QListView::doubleClicked, this, &ServerConfigDialog::editHotkey);
  connect(
      ui->listHotkeys->selectionModel(), &QItemSelectionModel::selectionChanged, this,
      &ServerConfigDialog::listHotkeysSelectionChanged
  );

  connect(ui->btnNewAction, &QPushButton::clicked, this, &ServerConfigDialog::addAction);
  connect(ui->btnEditAction, &QPushButton::clicked, this, &ServerConfigDialog::editAction);
  connect(ui->btnRemoveAction, &QPushButton::clicked, this, &ServerConfigDialog::removeAction);
  connect(ui->listActions, &QListView::doubleClicked, this, &ServerConfigDialog::editAction);
  connect(
      ui->listActions->selectionModel(), &QItemSelectionModel::selectionChanged, this,
      &ServerConfigDialog::listActionsSelectionChanged
  );

  connect(ui->rbProtocolBarrier, &QRadioButton::toggled, this, &ServerConfigDialog::toggleProtocol);
  connect(ui->cbHeartbeat, &QCheckBox::toggled, this, &ServerConfigDialog::toggleHeartbeat);
  connect(ui->sbHeartbeat, QOverload<int>::of(&QSpinBox::valueChanged), this, &ServerConfigDialog::setHeartbeat);
  connect(ui->cbWin32KeepForeground, &QCheckBox::toggled, this, &ServerConfigDialog::toggleWin32Foreground);
  connect(ui->cbSwitchDelay, &QCheckBox::toggled, this, &ServerConfigDialog::toggleSwitchDelay);
  connect(ui->sbSwitchDelay, QOverload<int>::of(&QSpinBox::valueChanged), this, &ServerConfigDialog::setSwitchDelay);
  connect(ui->cbSwitchDoubleTap, &QCheckBox::toggled, this, &ServerConfigDialog::toggleSwitchDoubleTap);
  connect(
      ui->sbSwitchDoubleTap, QOverload<int>::of(&QSpinBox::valueChanged), this, &ServerConfigDialog::setSwitchDoubleTap
  );

  connect(ui->cbRelativeMouseMoves, &QCheckBox::toggled, this, &ServerConfigDialog::toggleRelativeMouseMoves);
  connect(ui->cbEnableClipboard, &QCheckBox::toggled, this, &ServerConfigDialog::toggleClipboard);
  connect(ui->btnBrowseConfigFile, &QPushButton::clicked, this, &ServerConfigDialog::browseConfigFile);
  connect(ui->groupExternalConfig, &QGroupBox::toggled, this, &ServerConfigDialog::toggleExternalConfig);
  connect(
      ui->sbSwitchCornerSize, QOverload<int>::of(&QSpinBox::valueChanged), this,
      &ServerConfigDialog::setSwitchCornerSize
  );
  connect(
      ui->sbClipboardSizeLimit, QOverload<int>::of(&QSpinBox::valueChanged), this,
      &ServerConfigDialog::setClipboardLimit
  );
  connect(ui->cbCornerTopLeft, &QCheckBox::toggled, this, &ServerConfigDialog::toggleCornerTopLeft);
  connect(ui->cbCornerTopRight, &QCheckBox::toggled, this, &ServerConfigDialog::toggleCornerTopRight);
  connect(ui->cbCornerBottomLeft, &QCheckBox::toggled, this, &ServerConfigDialog::toggleCornerBottomLeft);
  connect(ui->cbCornerBottomRight, &QCheckBox::toggled, this, &ServerConfigDialog::toggleCornerBottomRight);
  connect(
      ui->cbDefaultLockToScreenState, &QCheckBox::toggled, this, &ServerConfigDialog::toggleDefaultLockToScreenState
  );
  connect(ui->cbDisableLockToScreen, &QCheckBox::toggled, this, &ServerConfigDialog::toggleLockToScreen);
}

QPointF ServerConfigDialog::nextFreePlacement() const
{
  // Place a new machine's default position to the right of everything
  // already on the canvas, with a gap so it doesn't accidentally touch (and
  // thus link to) an existing machine. Returned as a BOTTOM-left anchor
  // (x, baseline-Y) — real monitors of very different physical heights are
  // conventionally bottom-aligned (they all sit on the same desk), not
  // top-aligned, so every default placement targets the lowest bottom edge
  // among everything already on the canvas.
  QRectF unionRect;
  for (const auto &screen : canvasScene().screens()) {
    if (screen.isNull()) {
      continue;
    }
    const QRectF bbox = screen.boundingRect();
    if (bbox.isEmpty()) {
      continue;
    }
    unionRect = unionRect.isEmpty() ? bbox : unionRect.united(bbox);
  }
  constexpr qreal kGap = 60.0;
  return unionRect.isEmpty() ? QPointF(0, 0) : QPointF(unionRect.right() + kGap, unionRect.bottom());
}

void ServerConfigDialog::applyLiveMonitors(
    Screen &screen, const QList<deskflow::gui::CoreProcess::ReportedMonitor> &liveMonitors
)
{
  if (liveMonitors.isEmpty()) {
    return;
  }

  auto monitors = layoutMonitors(liveMonitors);
  QRectF groupBBox;
  for (const auto &monitor : monitors) {
    groupBBox = groupBBox.isEmpty() ? monitor.rect : groupBBox.united(monitor.rect);
  }

  // Only ever called for a screen with no layout yet (see callers), so
  // always place it fresh at the next free spot — never recompute an
  // existing (possibly user-arranged) layout in place.
  const QPointF translation = nextFreePlacement() - QPointF(groupBBox.left(), groupBBox.bottom());
  for (auto &monitor : monitors) {
    monitor.rect.translate(translation);
  }
  screen.setMonitors(monitors);
}

bool ServerConfigDialog::addComputer(const QString &clientName, bool doSilent)
{
  bool isAccepted = false;
  Screen newScreen(clientName);

  if (ScreenSettingsDialog dlg(this, &newScreen, &canvasScene().screens());
      doSilent || dlg.exec() == QDialog::Accepted) {
    constexpr qreal kDefaultW = 480.0;
    constexpr qreal kDefaultH = 320.0;
    const QPointF anchor = nextFreePlacement();
    const QPointF pos(anchor.x(), anchor.y() - kDefaultH);
    newScreen.setMonitors({gui::canvas::MonitorRect{QRectF(pos, QSizeF(kDefaultW, kDefaultH)), QString()}});

    canvasScene().addMachine(newScreen);
    isAccepted = true;
  }

  return isAccepted;
}

void ServerConfigDialog::onChange()
{
  bool isAppConfigDataEqual =
      m_originalServerConfigIsExternal == serverConfig().useExternalConfig() &&
      m_originalServerConfigUsesExternalFile == serverConfig().configFile() &&
      m_protocol == Settings::networkProtocol() &&
      m_enableHeartbeat == Settings::value(Settings::Server::EnableHeatbeat).toBool() &&
      m_enableSwitchDelay == Settings::value(Settings::Server::EnableSwitchDelay).toBool() &&
      m_enableSwitchDoubleTap == Settings::value(Settings::Server::EnableSwitchDoubleTap).toBool();
  ui->buttonBox->button(QDialogButtonBox::Ok)
      ->setEnabled(!isAppConfigDataEqual || !(m_originalServerConfig == m_serverConfig));
}
