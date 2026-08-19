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
  if (!screen || screen->isServer()) {
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
  // Sized by each screen's real physical dimensions (QScreen::physicalSize(),
  // in millimeters — the same unit basis applyLiveMonitors() approximates
  // for remote machines) rather than logical pixel count, so a large 27"
  // monitor actually looks bigger on the canvas than a small high-DPI laptop
  // panel with a similar or higher pixel count. Laid out purely left-to-right
  // in real x-order (the common case) so a machine's own monitors are always
  // placed edge-to-edge with no gap or overlap, regardless of how different
  // their pixel densities are.
  QList<gui::canvas::MonitorRect> localMonitors;
  {
    auto qscreens = QGuiApplication::screens();
    std::sort(qscreens.begin(), qscreens.end(), [](const QScreen *a, const QScreen *b) {
      return a->geometry().x() < b->geometry().x();
    });

    qreal xCursor = 0;
    for (const QScreen *qscreen : std::as_const(qscreens)) {
      const QRect geo = qscreen->geometry();
      QSizeF mm = qscreen->physicalSize();
      if (mm.width() <= 0 || mm.height() <= 0) {
        // Some virtual/headless screens report no physical size — fall back
        // to an assumed 96 DPI so it still renders at a sane size.
        mm = QSizeF(geo.width() / 96.0 * 25.4, geo.height() / 96.0 * 25.4);
      }
      // Bottom-aligned at a shared y=0 baseline: monitors of different
      // physical heights (e.g. an external display vs. a laptop panel)
      // conventionally sit on the same desk, not flush at the top.
      localMonitors.append(gui::canvas::MonitorRect{
          QRectF(QPointF(xCursor, -mm.height()), mm), QStringLiteral("%1x%2").arg(geo.width()).arg(geo.height())
      });
      xCursor += mm.width();
    }
  }
  if (localMonitors.isEmpty()) {
    localMonitors.append(gui::canvas::MonitorRect{QRectF(0, 0, 480, 320), QString()});
  }
  server->setMonitors(localMonitors);

  // Remote machines: populate from whatever the core process has last
  // reported for them (empty if never connected while this GUI was open).
  for (auto &screen : screens) {
    if (screen.isNull() || screen.isServer()) {
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

  // Prefer each monitor's own real physical size when the client reported
  // one; otherwise approximate at a standard 96 DPI so it's at least roughly
  // consistent in scale with the local machine's physical-size-based layout,
  // rather than using raw pixel counts as millimeters.
  constexpr qreal kAssumedDpi = 96.0;
  constexpr qreal kMmPerInch = 25.4;
  constexpr qreal kFallbackPxToMm = kMmPerInch / kAssumedDpi;

  // Build each monitor's rect relative to the group's own origin (mm), and
  // the group's own combined bounding box — this preserves the machine's
  // real relative arrangement among its own monitors.
  const QPoint origin = liveMonitors.first().rect.topLeft();
  QList<QRectF> relativeRects;
  QRectF groupBBox;
  for (const auto &monitor : liveMonitors) {
    const qreal scale = (monitor.mmSize.width() > 0 && monitor.mmSize.height() > 0)
                             ? static_cast<qreal>(monitor.mmSize.width()) / monitor.rect.width()
                             : kFallbackPxToMm;
    const QRectF relativePx = QRectF(monitor.rect).translated(-origin);
    const QRectF relativeMm(relativePx.topLeft() * scale, relativePx.size() * scale);
    relativeRects.append(relativeMm);
    groupBBox = groupBBox.isEmpty() ? relativeMm : groupBBox.united(relativeMm);
  }

  // Translate the whole group so it sits bottom-aligned at the target
  // anchor: preserve the machine's existing canvas position if it already
  // had one, otherwise place it fresh at the next free spot.
  const bool hadPreviousLayout = !screen.monitors().isEmpty();
  const QPointF anchor =
      hadPreviousLayout ? QPointF(screen.boundingRect().left(), screen.boundingRect().bottom()) : nextFreePlacement();
  const QPointF translation = anchor - QPointF(groupBBox.left(), groupBBox.bottom());

  QList<gui::canvas::MonitorRect> monitors;
  for (int i = 0; i < liveMonitors.size(); ++i) {
    monitors.append(gui::canvas::MonitorRect{
        relativeRects[i].translated(translation),
        QStringLiteral("%1x%2").arg(liveMonitors[i].rect.width()).arg(liveMonitors[i].rect.height())
    });
  }
  screen.setMonitors(monitors);
}

bool ServerConfigDialog::addComputer(const QString &clientName, bool doSilent)
{
  bool isAccepted = false;
  Screen newScreen(clientName);

  if (ScreenSettingsDialog dlg(this, &newScreen, &canvasScene().screens()); doSilent || dlg.exec() == QDialog::Accepted) {
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
