/* ScummVM - Graphic Adventure Engine
 *
 * ScummVM is the legal property of its developers, whose names
 * are too numerous to list here. Please refer to the COPYRIGHT
 * file distributed with this source distribution.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA.
 *
 */

#include <graphics/thumbnail.h>
#include <graphics/surface.h>
#include "common/debug-channels.h"
#include "common/winexe_pe.h"
#include "common/config-manager.h"

#include "engines/util.h"

#include "graphics/cursorman.h"

#include "pink/pink.h"
#include "pink/console.h"
#include "pink/objects/module.h"
#include "pink/objects/actors/lead_actor.h"
#include "pink/objects/sequences/sequencer.h"

namespace Pink {

Pink::PinkEngine::PinkEngine(OSystem *system, const ADGameDescription *desc)
		: Engine(system), _console(nullptr), _rnd("pink"),
		  _desc(*desc), _bro(nullptr), _module(nullptr),
		  _director(), _pdaMgr(this) {
	debug("PinkEngine constructed");

	DebugMan.addDebugChannel(kPinkDebugGeneral, "general", "General issues");
	DebugMan.addDebugChannel(kPinkDebugLoadingObjects, "loading_objects", "Serializing objects from Orb");
	DebugMan.addDebugChannel(kPinkDebugLoadingResources, "loading_resources", "Loading resources data");
	DebugMan.addDebugChannel(kPinkDebugGraphics, "graphics", "Graphics handling");
	DebugMan.addDebugChannel(kPinkDebugSound, "sound", "Sound processing");
}

Pink::PinkEngine::~PinkEngine() {
	delete _console;
	delete _bro;
	for (uint i = 0; i < _modules.size(); ++i) {
		delete _modules[i];
	}
	for (uint j = 0; j < _cursors.size(); ++j) {
		delete _cursors[j];
	}

	DebugMan.clearAllDebugChannels();
}

Common::Error PinkEngine::init() {
	debug("PinkEngine init");

	initGraphics(640, 480);

	_console = new Console(this);

	const Common::String orbName = _desc.filesDescriptions[0].fileName;
	const Common::String broName = _desc.filesDescriptions[1].fileName;

	if (strcmp(_desc.gameId, kPeril) == 0)
		_bro = new BroFile();
	else
		debug("This game doesn't need to use bro");

	if (!_orb.open(orbName) || (_bro && !_bro->open(broName, _orb.getTimestamp())))
		return Common::kNoGameDataFoundError;

	if (!loadCursors())
		return Common::kNoGameDataFoundError;

	setCursor(kLoadingCursor);
	_system->showMouse(1);

	_orb.loadGame(this);

	if (ConfMan.hasKey("save_slot"))
		loadGameState(ConfMan.getInt("save_slot"));
	else
		initModule(_modules[0]->getName(), "", nullptr);

	return Common::kNoError;
}

Common::Error Pink::PinkEngine::run() {
	Common::Error error = init();
	if (error.getCode() != Common::kNoError)
		return error;

	while (!shouldQuit()) {
		Common::Event event;
		while (_eventMan->pollEvent(event)) {
			switch (event.type) {
			case Common::EVENT_QUIT:
			case Common::EVENT_RTL:
				return Common::kNoError;
			case Common::EVENT_MOUSEMOVE:
				_actor->onMouseMove(event.mouse);
				break;
			case Common::EVENT_LBUTTONDOWN:
				_actor->onLeftButtonClick(event.mouse);
				break;
			case Common::EVENT_KEYDOWN:
					_actor->onKeyboardButtonClick(event.kbd.keycode);
				break;
				// don't know why it is used in original
			case Common::EVENT_LBUTTONUP:
			case Common::EVENT_RBUTTONDOWN:
			default:
				break;
			}
		}

		_actor->update();
		_director.update();
		_system->delayMillis(10);
	}

	return Common::kNoError;
}

void PinkEngine::load(Archive &archive) {
	archive.readString();
	archive.readString();
	_modules.deserialize(archive);
}

void PinkEngine::initModule(const Common::String &moduleName, const Common::String &pageName, Archive *saveFile) {
	if (_module) {
		for (uint i = 0; i < _modules.size(); ++i) {
			if (_module == _modules[i]) {
				_modules[i] = new ModuleProxy(_module->getName());

				delete _module;
				_module = nullptr;

				break;
			}
		}
	}

	for (uint i = 0; i < _modules.size(); ++i) {
		if (_modules[i]->getName() == moduleName) {
			loadModule(i);
			_module = static_cast<Module*>(_modules[i]);
			if (saveFile)
				_module->loadState(*saveFile);
			_module->init( saveFile ? kLoadingSave : kLoadingNewGame, pageName);
			break;
		}
	}
}

void PinkEngine::changeScene(Page *page) {
	setCursor(kLoadingCursor);
	if (!_nextModule.empty() && _nextModule.compareTo(_module->getName())) {
		initModule(_nextModule, _nextPage, nullptr);
	} else {
		assert(!_nextPage.empty());
		_module->changePage(_nextPage);
	}
}

void PinkEngine::setNextExecutors(const Common::String &nextModule, const Common::String &nextPage) {
	_nextModule = nextModule;
	_nextPage = nextPage;
}

void PinkEngine::loadModule(int index) {
	Module *module = new Module(this, _modules[index]->getName());

	_orb.loadObject(module, module->getName());

	delete _modules[index];
	_modules[index] = module;
}

bool PinkEngine::checkValueOfVariable(Common::String &variable, Common::String &value) {
	if (!_variables.contains(variable))
		return value == kUndefined;
	return _variables[variable] == value;
}

void PinkEngine::setVariable(Common::String &variable, Common::String &value) {
	_variables[variable] = value;
}

bool PinkEngine::loadCursors() {
	Common::PEResources exeResources;
	bool isPokus = !strcmp(_desc.gameId, kPokus);
	Common::String fileName = isPokus ? _desc.filesDescriptions[1].fileName : _desc.filesDescriptions[2].fileName;
	if (!exeResources.loadFromEXE(fileName))
		return false;

	_cursors.reserve(kCursorsCount);

	_cursors.push_back(Graphics::WinCursorGroup::createCursorGroup(exeResources, kPokusLoadingCursorID));
	_cursors.push_back(Graphics::WinCursorGroup::createCursorGroup(exeResources, kPokusExitForwardCursorID));
	_cursors.push_back(Graphics::WinCursorGroup::createCursorGroup(exeResources, kPokusExitLeftCursorID));
	_cursors.push_back(Graphics::WinCursorGroup::createCursorGroup(exeResources, kPokusExitRightCursorID));
	_cursors.push_back(Graphics::WinCursorGroup::createCursorGroup(exeResources, kPokusClickableFirstCursorID));
	_cursors.push_back(Graphics::WinCursorGroup::createCursorGroup(exeResources, kPokusClickableSecondCursorID));

	if (isPokus) {
		_cursors.push_back(Graphics::WinCursorGroup::createCursorGroup(exeResources, kPokusClickableThirdCursorID));
		_cursors.push_back(Graphics::WinCursorGroup::createCursorGroup(exeResources, kPokusNotClickableCursorID));
		_cursors.push_back(Graphics::WinCursorGroup::createCursorGroup(exeResources, kPokusHoldingItemCursorID));
	} else {
		_cursors.push_back(Graphics::WinCursorGroup::createCursorGroup(exeResources, kPerilClickableThirdCursorID));
		_cursors.push_back(Graphics::WinCursorGroup::createCursorGroup(exeResources, kPerilNotClickableCursorID));
		_cursors.push_back(Graphics::WinCursorGroup::createCursorGroup(exeResources, kPerilHoldingItemCursorID));
	}

	_cursors.push_back(Graphics::WinCursorGroup::createCursorGroup(exeResources, kPokusPDADefaultCursorID));

	if (isPokus) {
		_cursors.push_back(Graphics::WinCursorGroup::createCursorGroup(exeResources, kPokusPDAClickableFirstFrameCursorID));
		_cursors.push_back(Graphics::WinCursorGroup::createCursorGroup(exeResources, kPokusPDAClickableSecondFrameCursorID));
	} else {
		_cursors.push_back(Graphics::WinCursorGroup::createCursorGroup(exeResources, kPerilPDAClickableFirstFrameCursorID));
		_cursors.push_back(Graphics::WinCursorGroup::createCursorGroup(exeResources, kPerilPDAClickableSecondFrameCursorID));
	}
	return true;
}

void PinkEngine::setCursor(uint cursorIndex) {
	Graphics::Cursor *cursor = _cursors[cursorIndex]->cursors[0].cursor;
	_system->setCursorPalette(cursor->getPalette(), cursor->getPaletteStartIndex(), cursor->getPaletteCount());
	_system->setMouseCursor(cursor->getSurface(), cursor->getWidth(), cursor->getHeight(),
							cursor->getHotspotX(), cursor->getHotspotY(), cursor->getKeyColor());
}

Common::Error PinkEngine::loadGameState(int slot) {
	Common::SeekableReadStream *in = _saveFileMan->openForLoading(generateSaveName(slot, _desc.gameId));
	if (!in)
		return Common::kNoGameDataFoundError;

	SaveStateDescriptor desc;
	if (!readSaveHeader(*in, desc))
		return Common::kUnknownError;

	Archive archive(in);
	_variables.deserialize(archive);
	_nextModule = archive.readString();
	_nextPage = archive.readString();
	initModule(archive.readString(), "", &archive);

	return Common::kNoError;
}

bool PinkEngine::canLoadGameStateCurrently() {
	return true;
}

Common::Error PinkEngine::saveGameState(int slot, const Common::String &desc) {
	Common::OutSaveFile *out = _saveFileMan->openForSaving(generateSaveName(slot, _desc.gameId));
	if (!out)
		return Common::kUnknownError;

	Archive archive(out);

	out->write("pink", 4);
	archive.writeString(desc);

	TimeDate curTime;
	_system->getTimeAndDate(curTime);

	out->writeUint32LE(((curTime.tm_mday & 0xFF) << 24) | (((curTime.tm_mon + 1) & 0xFF) << 16) | ((curTime.tm_year + 1900) & 0xFFFF));
	out->writeUint16LE(((curTime.tm_hour & 0xFF) << 8) | ((curTime.tm_min) & 0xFF));

	out->writeUint32LE(getTotalPlayTime() / 1000);

	if (!Graphics::saveThumbnail(*out))
		return Common::kUnknownError;

	_variables.serialize(archive);
	archive.writeString(_nextModule);
	archive.writeString(_nextPage);

	archive.writeString(_module->getName());
	_module->saveState(archive);

	delete out;

	return Common::kNoError;
}

bool PinkEngine::canSaveGameStateCurrently() {
	return true;
}

bool PinkEngine::hasFeature(Engine::EngineFeature f) const {
	return
			f == kSupportsRTL ||
			f == kSupportsLoadingDuringRuntime ||
			f == kSupportsSavingDuringRuntime;
}

void PinkEngine::pauseEngineIntern(bool pause) {
	Engine::pauseEngineIntern(pause);
	_director.pause(pause);
	_system->showMouse(!pause);
}

PDAMgr &PinkEngine::getPdaMgr() {
	return _pdaMgr;
}

Common::String generateSaveName(int slot, const char *gameId) {
	return Common::String::format("%s.s%02d", gameId, slot);
}

bool readSaveHeader(Common::InSaveFile &in, SaveStateDescriptor &desc) {
	char pink[4];
	in.read(&pink, 4);
	if (strcmp(pink, "pink"))
		return false;

	const Common::String description = in.readPascalString();
	uint32 date = in.readUint32LE();
	uint16 time = in.readUint16LE();
	uint32 playTime = in.readUint32LE();
	if (!Graphics::checkThumbnailHeader(in))
		return false;

	Graphics::Surface *thumbnail;
	if (!Graphics::loadThumbnail(in, thumbnail))
		return false;

	int day = (date >> 24) & 0xFF;
	int month = (date >> 16) & 0xFF;
	int year = date & 0xFFFF;

	int hour = (time >> 8) & 0xFF;
	int minutes = time & 0xFF;

	desc.setSaveDate(year, month, day);
	desc.setSaveTime(hour, minutes);
	desc.setPlayTime(playTime * 1000);
	desc.setDescription(description);
	desc.setThumbnail(thumbnail);

	return true;
}

}