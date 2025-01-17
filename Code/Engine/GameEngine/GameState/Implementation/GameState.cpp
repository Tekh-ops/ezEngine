
#include <GameEngine/GameEnginePCH.h>

#include <Core/ActorSystem/Actor.h>
#include <Core/ActorSystem/ActorManager.h>
#include <Core/ActorSystem/ActorPluginWindow.h>
#include <Core/GameState/GameStateWindow.h>
#include <Core/Prefabs/PrefabResource.h>
#include <Core/World/World.h>
#include <Foundation/Configuration/Singleton.h>
#include <Foundation/IO/FileSystem/FileSystem.h>
#include <Foundation/System/Screen.h>
#include <Foundation/Utilities/CommandLineOptions.h>
#include <GameEngine/Configuration/RendererProfileConfigs.h>
#include <GameEngine/Configuration/XRConfig.h>
#include <GameEngine/GameApplication/GameApplication.h>
#include <GameEngine/GameApplication/WindowOutputTarget.h>
#include <GameEngine/Gameplay/PlayerStartPointComponent.h>
#include <GameEngine/XR/XRInterface.h>
#include <GameEngine/XR/XRRemotingInterface.h>
#include <RendererCore/Pipeline/RenderPipelineResource.h>
#include <RendererCore/Pipeline/View.h>
#include <RendererCore/RenderWorld/RenderWorld.h>
#include <RendererFoundation/Device/Device.h>
#include <RendererFoundation/Device/SwapChain.h>

// clang-format off
EZ_BEGIN_DYNAMIC_REFLECTED_TYPE(ezGameState, 1, ezRTTINoAllocator)
EZ_END_DYNAMIC_REFLECTED_TYPE;

EZ_STATICLINK_FILE(GameEngine, GameEngine_GameState_Implementation_GameState);
// clang-format on

ezGameState::ezGameState() {}

ezGameState::~ezGameState() {}

void ezGameState::OnActivation(ezWorld* pWorld, const ezTransform* pStartPosition)
{
  m_pMainWorld = pWorld;
  {
    ConfigureMainCamera();

    CreateActors();
  }

  ConfigureInputActions();

  SpawnPlayer(pStartPosition).IgnoreResult();
}

void ezGameState::OnDeactivation()
{
  if (m_bXREnabled)
  {
    m_bXREnabled = false;
    ezXRInterface* pXRInterface = ezSingletonRegistry::GetSingletonInstance<ezXRInterface>();
    ezActorManager::GetSingleton()->DestroyAllActors(pXRInterface);
    pXRInterface->Deinitialize();

    if (ezXRRemotingInterface* pXRRemotingInterface = ezSingletonRegistry::GetSingletonInstance<ezXRRemotingInterface>())
    {
      if (pXRRemotingInterface->Deinitialize().Failed())
      {
        ezLog::Error("Failed to deinitialize ezXRRemotingInterface, make sure all actors are destroyed and ezXRInterface deinitialized.");
      }
    }
  }

  ezRenderWorld::DeleteView(m_hMainView);
}

void ezGameState::ScheduleRendering()
{
  ezRenderWorld::AddMainView(m_hMainView);
}

ezUniquePtr<ezActor> ezGameState::CreateXRActor()
{
  EZ_LOG_BLOCK("CreateXRActor");
  // Init XR
  const ezXRConfig* pConfig = ezGameApplicationBase::GetGameApplicationBaseInstance()->GetPlatformProfile().GetTypeConfig<ezXRConfig>();
  if (!pConfig)
    return nullptr;

  if (!pConfig->m_bEnableXR)
    return nullptr;

  ezXRInterface* pXRInterface = ezSingletonRegistry::GetSingletonInstance<ezXRInterface>();
  if (!pXRInterface)
  {
    ezLog::Error("No ezXRInterface interface found. Please load a XR plugin to enable XR.");
    return nullptr;
  }

  ezXRRemotingInterface* pXRRemotingInterface = ezSingletonRegistry::GetSingletonInstance<ezXRRemotingInterface>();
  if (ezXRRemotingInterface::cvar_XrRemoting)
  {
    if (pXRRemotingInterface)
    {
      if (pXRRemotingInterface->Initialize().Failed())
      {
        ezLog::Error("ezXRRemotingInterface could not be initialized. See log for details.");
      }
      else
      {
        m_bXRRemotingEnabled = true;
      }
    }
    else
    {
      ezLog::Error("No ezXRRemotingInterface interface found. Please load a XR remoting plugin to enable XR Remoting.");
    }
  }

  if (pXRInterface->Initialize().Failed())
  {
    ezLog::Error("ezXRInterface could not be initialized. See log for details.");
    return nullptr;
  }
  m_bXREnabled = true;

  ezUniquePtr<ezWindow> pMainWindow;
  ezUniquePtr<ezWindowOutputTargetBase> pOutput;

  if (pXRInterface->SupportsCompanionView())
  {
    // XR Window with added companion window (allows keyboard / mouse input).
    pMainWindow = CreateMainWindow();
    EZ_ASSERT_DEV(pMainWindow != nullptr, "To change the main window creation behavior, override ezGameState::CreateActors().");
    pOutput = CreateMainOutputTarget(pMainWindow.Borrow());
    ConfigureMainWindowInputDevices(pMainWindow.Borrow());
    SetupMainView(pOutput.Borrow(), pMainWindow->GetClientAreaSize());
  }
  else
  {
    // XR Window (no companion window)
    SetupMainView(nullptr, {});
  }

  if (m_bXRRemotingEnabled)
  {
    if (pXRRemotingInterface->Connect(ezXRRemotingInterface::cvar_XrRemotingHostName.GetValue().GetData()).Failed())
    {
      ezLog::Error("Failed to connect XR Remoting.");
    }
  }

  ezView* pView = nullptr;
  EZ_VERIFY(ezRenderWorld::TryGetView(m_hMainView, pView), "");
  ezUniquePtr<ezActor> pXRActor = pXRInterface->CreateActor(pView, ezGALMSAASampleCount::Default, std::move(pMainWindow), std::move(pOutput));
  return std::move(pXRActor);
}

void ezGameState::CreateActors()
{
  EZ_LOG_BLOCK("CreateActors");
  ezUniquePtr<ezActor> pXRActor = CreateXRActor();
  if (pXRActor != nullptr)
  {
    ezActorManager::GetSingleton()->AddActor(std::move(pXRActor));
    return;
  }

  ezUniquePtr<ezWindow> pMainWindow = CreateMainWindow();
  EZ_ASSERT_DEV(pMainWindow != nullptr, "To change the main window creation behavior, override ezGameState::CreateActors().");
  ezUniquePtr<ezWindowOutputTargetBase> pOutput = CreateMainOutputTarget(pMainWindow.Borrow());
  ConfigureMainWindowInputDevices(pMainWindow.Borrow());
  SetupMainView(pOutput.Borrow(), pMainWindow->GetClientAreaSize());

  {
    // Default flat window
    ezUniquePtr<ezActorPluginWindowOwner> pWindowPlugin = EZ_DEFAULT_NEW(ezActorPluginWindowOwner);
    pWindowPlugin->m_pWindow = std::move(pMainWindow);
    pWindowPlugin->m_pWindowOutputTarget = std::move(pOutput);
    ezUniquePtr<ezActor> pActor = EZ_DEFAULT_NEW(ezActor, "Main Window", this);
    pActor->AddPlugin(std::move(pWindowPlugin));
    ezActorManager::GetSingleton()->AddActor(std::move(pActor));
  }
}

void ezGameState::ConfigureMainWindowInputDevices(ezWindow* pWindow) {}

void ezGameState::ConfigureInputActions() {}

void ezGameState::SetupMainView(ezWindowOutputTargetBase* pOutputTarget, ezSizeU32 viewportSize)
{
  if (m_bXREnabled)
  {
    const ezXRConfig* pConfig = ezGameApplicationBase::GetGameApplicationBaseInstance()->GetPlatformProfile().GetTypeConfig<ezXRConfig>();
    ezXRInterface* pXRInterface = ezSingletonRegistry::GetSingletonInstance<ezXRInterface>();

    auto renderPipeline = ezResourceManager::LoadResource<ezRenderPipelineResource>(pConfig->m_sXRRenderPipeline);
    CreateMainView(renderPipeline);
    // Render target setup is done by ezXRInterface::CreateActor
  }
  else
  {
    const auto* pConfig = ezGameApplicationBase::GetGameApplicationBaseInstance()->GetPlatformProfile().GetTypeConfig<ezRenderPipelineProfileConfig>();
    auto renderPipeline = ezResourceManager::LoadResource<ezRenderPipelineResource>(pConfig->m_sMainRenderPipeline);
    ezView* pView = CreateMainView(renderPipeline);

    // Render target setup
    {
      ezWindowOutputTargetGAL* pOutputGAL = static_cast<ezWindowOutputTargetGAL*>(pOutputTarget);
      const ezGALSwapChain* pSwapChain = ezGALDevice::GetDefaultDevice()->GetSwapChain(pOutputGAL->m_hSwapChain);
      ezGALRenderTargetViewHandle hBackBuffer = ezGALDevice::GetDefaultDevice()->GetDefaultRenderTargetView(pSwapChain->GetBackBufferTexture());
      ezGALRenderTargetSetup renderTargetSetup;
      renderTargetSetup.SetRenderTarget(0, hBackBuffer);
      pView->SetRenderTargetSetup(renderTargetSetup);
      pView->SetViewport(ezRectFloat(0.0f, 0.0f, (float)viewportSize.width, (float)viewportSize.height));
    }
  }
}

ezView* ezGameState::CreateMainView(ezTypedResourceHandle<ezRenderPipelineResource> hRenderPipeline)
{
  EZ_LOG_BLOCK("CreateMainView");
  ezView* pView = nullptr;
  m_hMainView = ezRenderWorld::CreateView("MainView", pView);
  pView->SetCameraUsageHint(ezCameraUsageHint::MainView);
  pView->SetRenderPipelineResource(hRenderPipeline);
  pView->SetWorld(m_pMainWorld);
  pView->SetCamera(&m_MainCamera);
  ezRenderWorld::AddMainView(m_hMainView);

  const ezTag& tagEditor = ezTagRegistry::GetGlobalRegistry().RegisterTag("Editor");
  // exclude all editor objects from rendering in proper game views
  pView->m_ExcludeTags.Set(tagEditor);
  return pView;
}

ezResult ezGameState::SpawnPlayer(const ezTransform* pStartPosition)
{
  if (m_pMainWorld == nullptr)
    return EZ_FAILURE;

  EZ_LOCK(m_pMainWorld->GetWriteMarker());

  ezPlayerStartPointComponentManager* pMan = m_pMainWorld->GetComponentManager<ezPlayerStartPointComponentManager>();
  if (pMan == nullptr)
    return EZ_FAILURE;

  for (auto it = pMan->GetComponents(); it.IsValid(); ++it)
  {
    if (it->IsActive() && it->GetPlayerPrefab().IsValid())
    {
      ezResourceLock<ezPrefabResource> pPrefab(it->GetPlayerPrefab(), ezResourceAcquireMode::BlockTillLoaded);

      if (pPrefab.GetAcquireResult() == ezResourceAcquireResult::Final)
      {
        const ezUInt16 uiTeamID = it->GetOwner()->GetTeamID();
        ezTransform startPos = it->GetOwner()->GetGlobalTransform();

        if (pStartPosition)
        {
          startPos = *pStartPosition;
          startPos.m_vScale.Set(1.0f);
          startPos.m_vPosition.z += 1.0f; // do not spawn player prefabs on the ground, they may not have their origin there
        }

        ezPrefabInstantiationOptions options;
        options.m_pOverrideTeamID = &uiTeamID;

        pPrefab->InstantiatePrefab(*m_pMainWorld, startPos, options, &(it->m_Parameters));

        return EZ_SUCCESS;
      }
    }
  }

  return EZ_FAILURE;
}

void ezGameState::ChangeMainWorld(ezWorld* pNewMainWorld)
{
  m_pMainWorld = pNewMainWorld;

  ezView* pView = nullptr;
  if (ezRenderWorld::TryGetView(m_hMainView, pView))
  {
    pView->SetWorld(m_pMainWorld);
  }
}

void ezGameState::ConfigureMainCamera()
{
  ezVec3 vCameraPos = ezVec3(0.0f, 0.0f, 0.0f);

  ezCoordinateSystem coordSys;

  if (m_pMainWorld)
  {
    m_pMainWorld->GetCoordinateSystem(vCameraPos, coordSys);
  }
  else
  {
    coordSys.m_vForwardDir.Set(1, 0, 0);
    coordSys.m_vRightDir.Set(0, 1, 0);
    coordSys.m_vUpDir.Set(0, 0, 1);
  }

  // if the camera is already set to be in 'Stereo' mode, its parameters are set from the outside
  if (m_MainCamera.GetCameraMode() != ezCameraMode::Stereo)
  {
    m_MainCamera.LookAt(vCameraPos, vCameraPos + coordSys.m_vForwardDir, coordSys.m_vUpDir);
    m_MainCamera.SetCameraMode(ezCameraMode::PerspectiveFixedFovY, 60.0f, 0.1f, 1000.0f);
  }
}

ezCommandLineOptionPath opt_Window("GameState", "-wnd", "Path to the window configuration file to use.", "");

ezUniquePtr<ezWindow> ezGameState::CreateMainWindow()
{
  if (false)
  {
    ezHybridArray<ezScreenInfo, 2> screens;
    ezScreen::EnumerateScreens(screens).IgnoreResult();
    ezScreen::PrintScreenInfo(screens);
  }

  ezStringBuilder sWndCfg = opt_Window.GetOptionValue(ezCommandLineOption::LogMode::AlwaysIfSpecified);

  if (!sWndCfg.IsEmpty() && !ezFileSystem::ExistsFile(sWndCfg))
  {
    ezLog::Dev("Window Config file does not exist: '{0}'", sWndCfg);
    sWndCfg.Clear();
  }

  if (sWndCfg.IsEmpty())
  {
    if (ezFileSystem::ExistsFile(":appdata/Window.ddl"))
      sWndCfg = ":appdata/Window.ddl";
    else
      sWndCfg = ":project/Window.ddl";
  }

  ezWindowCreationDesc wndDesc;
  wndDesc.LoadFromDDL(sWndCfg).IgnoreResult();

  if (m_bXREnabled)
  {
    wndDesc.m_bClipMouseCursor = false;
    wndDesc.m_bShowMouseCursor = true;
    wndDesc.m_WindowMode = ezWindowMode::WindowResizable;
  }

  ezUniquePtr<ezGameStateWindow> pWindow = EZ_DEFAULT_NEW(ezGameStateWindow, wndDesc, [] {});
  pWindow->ResetOnClickClose([this]() { this->RequestQuit(); });
  if (pWindow->GetInputDevice())
    pWindow->GetInputDevice()->SetMouseSpeed(ezVec2(0.002f));

  return pWindow;
}

ezUniquePtr<ezWindowOutputTargetBase> ezGameState::CreateMainOutputTarget(ezWindow* pMainWindow)
{
  ezUniquePtr<ezWindowOutputTargetGAL> pOutput = EZ_DEFAULT_NEW(ezWindowOutputTargetGAL);

  ezGALSwapChainCreationDescription desc;
  desc.m_pWindow = pMainWindow;
  desc.m_BackBufferFormat = ezGALResourceFormat::RGBAUByteNormalizedsRGB;
  desc.m_bAllowScreenshots = true;

  pOutput->CreateSwapchain(desc);

  return pOutput;
}
