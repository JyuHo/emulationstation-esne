#include "ContentInstaller.h"
#include "Window.h"
#include "components/AsyncNotificationComponent.h"
#include "utils/StringUtil.h"
#include "ApiSystem.h"
#include "LocaleES.h"
#if ESNE
#include "guis/GuiMenu.h"
#endif

#define ICONINDEX _U("\uF019 ")

ContentInstaller*						ContentInstaller::mInstance = nullptr;
std::mutex								ContentInstaller::mLock;
std::list<std::pair<int, std::string>>	ContentInstaller::mQueue;
std::list<std::pair<int, std::string>>	ContentInstaller::mProcessingQueue;
std::list<IContentInstalledNotify*>		ContentInstaller::mNotification;
#if ESNE
bool									ContentInstaller::mStoreChanged = false;
int										ContentInstaller::mStoreOpen = 0;
#endif

void ContentInstaller::Enqueue(Window* window, ContentType type, const std::string contentName)
{
	std::unique_lock<std::mutex> lock(mLock);

	for (auto item : mProcessingQueue)
		if (item.first == type && item.second == contentName)
			return;

	for (auto item : mQueue)
		if (item.first == type && item.second == contentName)
			return;

	mQueue.push_back(std::pair<int, std::string>(type, contentName));

	if (mInstance == nullptr)
		mInstance = new ContentInstaller(window);

	mInstance->updateNotificationComponentTitle(true);
}

bool ContentInstaller::IsInQueue(ContentType type, const std::string contentName)
{
	std::unique_lock<std::mutex> lock(mLock);

	for (auto item : mProcessingQueue)
		if (item.first == type && item.second == contentName)
			return true;

	for (auto item : mQueue)
		if (item.first == type && item.second == contentName)
			return true;

	return false;
}

ContentInstaller::ContentInstaller(Window* window)
{
	mInstance = this;

	mCurrent = 0;
	mQueueSize = 0;

	mWindow = window;

	mWndNotification = mWindow->createAsyncNotificationComponent();
	mHandle = new std::thread(&ContentInstaller::threadUpdate, this);
}

ContentInstaller::~ContentInstaller()
{
	mHandle = nullptr;

	mWndNotification->close();
	mWndNotification = nullptr;
}

void ContentInstaller::updateNotificationComponentTitle(bool incQueueSize)
{
	if (incQueueSize)
		mQueueSize++;

	std::string cnt = " " + std::to_string(mCurrent) + "/" + std::to_string(mQueueSize);
	mWndNotification->updateTitle(ICONINDEX + _("DOWNLOADING")+ cnt);
}

void ContentInstaller::updateNotificationComponentContent(const std::string info)
{
	auto pos = info.find(">>>");
	if (pos != std::string::npos)
	{
		std::string percent(info.substr(pos));
		percent = Utils::String::replace(percent, ">", "");
		percent = Utils::String::replace(percent, "%", "");
		percent = Utils::String::replace(percent, " ", "");

		int value = atoi(percent.c_str());

		std::string text(info.substr(0, pos));
		text = Utils::String::trim(text);

		mWndNotification->updatePercent(value);
		mWndNotification->updateText(text);
	}
	else
	{
		mWndNotification->updatePercent(-1);
		mWndNotification->updateText(info);
	}
}

void ContentInstaller::threadUpdate()
{
	mCurrent = 0;

	// Wait for an event to say there is something in the queue
	std::unique_lock<std::mutex> lock(mLock);

	while (true)
	{
		if (mQueue.empty())
			break;

		mCurrent++;
		updateNotificationComponentTitle(false);

		auto data = mQueue.front();
		mQueue.pop_front();
		mProcessingQueue.push_back(data);

		lock.unlock();

		std::pair<std::string, int> updateStatus;
		bool success = false;

		if (data.first == ContentType::CONTENT_THEME_INSTALL)
		{
			updateStatus = ApiSystem::getInstance()->installBatoceraTheme(data.second, [this](const std::string info)
			{
				updateNotificationComponentContent(info);
			});

			if (updateStatus.second == 0)
			{
				success = true;
				mWindow->displayNotificationMessage(ICONINDEX + data.second + " : " + _("THEME INSTALLED SUCCESSFULLY"));
			}
			else
			{
				std::string error = _("AN ERROR OCCURRED") + std::string(": ") + updateStatus.first;
				mWindow->displayNotificationMessage(ICONINDEX + error);
			}

		}
		else if (data.first == ContentType::CONTENT_THEME_UNINSTALL)
		{
			updateStatus = ApiSystem::getInstance()->uninstallBatoceraTheme(data.second, [this](const std::string info)
			{
				updateNotificationComponentContent(info);
			});

			if (updateStatus.second == 0)
			{
				success = true;
				mWindow->displayNotificationMessage(ICONINDEX + data.second + " : " + _("THEME UNINSTALLED SUCCESSFULLY"));
			}
			else
			{
				std::string error = _("AN ERROR OCCURRED") + std::string(": ") + updateStatus.first;
				mWindow->displayNotificationMessage(ICONINDEX + error);
			}

		}
		else if (data.first == ContentType::CONTENT_BEZEL_INSTALL)
		{
			updateStatus = ApiSystem::getInstance()->installBatoceraBezel(data.second, [this](const std::string info)
			{
				updateNotificationComponentContent(info);
			});

			if (updateStatus.second == 0)
			{
				success = true;
				mWindow->displayNotificationMessage(ICONINDEX + data.second + " : " + _("BEZELS INSTALLED SUCCESSFULLY"));
			}
			else
			{
				std::string error = _("AN ERROR OCCURRED") + std::string(": ") + updateStatus.first;
				mWindow->displayNotificationMessage(ICONINDEX + error);
			}
		}
		else if (data.first == ContentType::CONTENT_BEZEL_UNINSTALL)
		{
			updateStatus = ApiSystem::getInstance()->uninstallBatoceraBezel(data.second, [this](const std::string info)
			{
				updateNotificationComponentContent(info);
			});

			if (updateStatus.second == 0)
			{
				success = true;
				mWindow->displayNotificationMessage(ICONINDEX + data.second + " : " + _("BEZELS UNINSTALLED SUCCESSFULLY"));
			}
			else
			{
				std::string error = _("AN ERROR OCCURRED") + std::string(": ") + updateStatus.first;
				mWindow->displayNotificationMessage(ICONINDEX + error);
			}
		}
		else if (data.first == ContentType::CONTENT_STORE_INSTALL || data.first == ContentType::CONTENT_EMULATOR_INSTALL)
		{
			updateStatus = ApiSystem::getInstance()->installBatoceraStorePackage(data.second, [this](const std::string info)
			{
				updateNotificationComponentContent(info);
			}, data.first == ContentType::CONTENT_EMULATOR_INSTALL ? "batocera-emulators" : "batocera-store");

			if (updateStatus.second == 0)
			{
				success = true;
				mWindow->displayNotificationMessage(ICONINDEX + data.second + " : " + _("PACKAGE INSTALLED SUCCESSFULLY"));
			}
			else
			{
				std::string error = _("AN ERROR OCCURRED") + std::string(": ") + updateStatus.first;
				mWindow->displayNotificationMessage(ICONINDEX + error);
			}
		}
		else if (data.first == ContentType::CONTENT_STORE_UNINSTALL || data.first == ContentType::CONTENT_EMULATOR_UNINSTALL)
		{
			updateStatus = ApiSystem::getInstance()->uninstallBatoceraStorePackage(data.second, [this](const std::string info)
			{
				updateNotificationComponentContent(info);
			}, data.first == ContentType::CONTENT_EMULATOR_UNINSTALL ? "batocera-emulators" : "batocera-store");

			if (updateStatus.second == 0)
			{
				success = true;
				mWindow->displayNotificationMessage(ICONINDEX + data.second + " : " + _("PACKAGE REMOVED SUCCESSFULLY"));
			}
			else
			{
				std::string error = _("AN ERROR OCCURRED") + std::string(": ") + updateStatus.first;
				mWindow->displayNotificationMessage(ICONINDEX + error);
			}
		}

		OnContentInstalled(data.first, data.second, success);

		lock.lock();
		mProcessingQueue.remove(data);
#if ESNE
		// emulators: esctl rewrote es_systems.cfg, reloaded with the game lists
		if (success && (data.first == ContentType::CONTENT_STORE_INSTALL || data.first == ContentType::CONTENT_STORE_UNINSTALL ||
			data.first == ContentType::CONTENT_EMULATOR_INSTALL || data.first == ContentType::CONTENT_EMULATOR_UNINSTALL))
			mStoreChanged = true;
#endif
	}

#if ESNE
	reloadGamesIfIdle(mWindow, true);
#endif

	delete this;
	mInstance = nullptr;
}

void ContentInstaller::OnContentInstalled(int contentType, std::string contentName, bool success)
{
	std::unique_lock<std::mutex> lock(mLock);

	for (IContentInstalledNotify* n : mNotification)
		n->OnContentInstalled(contentType, contentName, success);
}

void ContentInstaller::RegisterNotify(IContentInstalledNotify* instance)
{
	std::unique_lock<std::mutex> lock(mLock);
	mNotification.push_back(instance);
}

void ContentInstaller::UnregisterNotify(IContentInstalledNotify* instance)
{
	std::unique_lock<std::mutex> lock(mLock);

	for (auto it = mNotification.cbegin(); it != mNotification.cend(); it++)
	{
		if ((*it) == instance)
		{
			mNotification.erase(it);
			return;
		}
	}
}

#if ESNE
void ContentInstaller::SetStoreOpen(Window* window, bool open)
{
	std::unique_lock<std::mutex> lock(mLock);

	mStoreOpen += open ? 1 : -1;
	if (!open)
		reloadGamesIfIdle(window, false);
}

void ContentInstaller::reloadGamesIfIdle(Window* window, bool queueFinished)
{
	if (!mStoreChanged || mStoreOpen > 0 || (!queueFinished && mInstance != nullptr))
		return;

	mStoreChanged = false;
	window->postToUiThread([window]() { GuiMenu::updateGameLists(window, false); });
}
#endif
