#ifndef __VIRTUAL_INPUT__
#define __VIRTUAL_INPUT__

#include "Module.h"

namespace WPEFramework {
namespace PluginHost {

   class EXTERNAL VirtualInput {
    private:

 enum enumModifier {
            LEFTSHIFT = 0, //  0..3  bits are for LeftShift reference counting (max 15)
            RIGHTSHIFT = 4, //  4..7  bits are for RightShift reference counting (max 15)
            LEFTALT = 8, //  8..11 bits are for LeftAlt reference counting (max 15)
            RIGHTALT = 12, // 12..15 bits are for RightAlt reference counting (max 15)
            LEFTCTRL = 16, // 16..19 bits are for LeftCtrl reference counting (max 15)
            RIGHTCTRL = 20 // 20..23 bits are for RightCtrl reference counting (max 15)
        };
        
        class RepeatKeyTimer : public Core::IDispatch {
        private:
            RepeatKeyTimer() = delete;
            RepeatKeyTimer(const RepeatKeyTimer&) = delete;
            RepeatKeyTimer& operator=(const RepeatKeyTimer&) = delete;

        public:
#ifdef __WIN32__
#pragma warning(disable : 4355)
#endif
            RepeatKeyTimer(VirtualInput* parent)
                : _parent(*parent)
                , _adminLock()
                , _startTime(0)
                , _intervalTime(0)
                , _code(~0)
                , _nextSlot()
                , _job()
            {
            }
            
#ifdef __WIN32__
#pragma warning(default : 4355)
#endif
            ~RepeatKeyTimer() override
            {
            }

        public:
            void AddReference()
            {
                _job = Core::ProxyType<Core::IDispatch>(static_cast<Core::IDispatch&>(*this));
            }

            void DropReference()
            {
                Reset();
                Core::WorkerPool::Instance().Revoke(_job);
                _job.Release();
            }
            void Interval(const uint16_t startTime, const uint16_t intervalTime)
            {
                _startTime = startTime;
                _intervalTime = intervalTime;
            }
            void Arm(const uint32_t code)
            {
                ASSERT(_code == static_cast<uint32_t>(~0));

                if (_startTime != 0) {
      
                    _nextSlot = Core::Time::Now().Add(_startTime);
                    
                    Core::WorkerPool::Instance().Revoke(_job);
                    
                    _adminLock.Lock();

                    _code = code;

                    _adminLock.Unlock();

                    Core::WorkerPool::Instance().Schedule(_nextSlot, _job);
                }
            }          
            uint32_t Reset()
            {
                _adminLock.Lock();
                uint32_t result = _code;
                _code = ~0;
                _adminLock.Unlock();
                return (result);
            } 

        private:

            void Dispatch() override
            {
                _adminLock.Lock();

                if(_code != static_cast<uint32_t>(~0)) {
                    uint16_t code = static_cast<uint16_t>(_code & 0xFFFF);

                    ASSERT(_nextSlot.IsValid());

                    _parent.RepeatKey(code);

                    _nextSlot.Add(_intervalTime);
                    
                    Core::WorkerPool::Instance().Schedule(_nextSlot, _job);
                }

                _adminLock.Unlock();

            }

        private:
            VirtualInput& _parent;
            Core::CriticalSection _adminLock;
            uint16_t _startTime;
            uint16_t _intervalTime;
            uint32_t _code;
            Core::Time _nextSlot;
            Core::ProxyType<Core::IDispatch> _job;    
        };

    public:
        class EXTERNAL KeyMap {
        public:
            enum modifier {
                LEFTSHIFT = 0x01,
                RIGHTSHIFT = 0x02,
                LEFTALT = 0x04,
                RIGHTALT = 0x08,
                LEFTCTRL = 0x10,
                RIGHTCTRL = 0x20
            };

        private:
            typedef Core::JSON::EnumType<modifier> JSONModifier;

            KeyMap(const KeyMap&) = delete;
            KeyMap& operator=(const KeyMap&) = delete;

        public:
            class KeyMapEntry : public Core::JSON::Container {
            private:
                KeyMapEntry& operator=(const KeyMapEntry&) = delete;

            public:
                inline KeyMapEntry()
                    : Core::JSON::Container()
                {
                    Add(_T("code"), &Code);
                    Add(_T("key"), &Key);
                    Add(_T("modifiers"), &Modifiers);
                }
                inline KeyMapEntry(const KeyMapEntry& copy)
                    : Core::JSON::Container()
                    , Code(copy.Code)
                    , Key(copy.Key)
                    , Modifiers(copy.Modifiers)
                {
                    Add(_T("code"), &Code);
                    Add(_T("key"), &Key);
                    Add(_T("modifiers"), &Modifiers);
                }
                virtual ~KeyMapEntry()
                {
                }

            public:
                Core::JSON::HexUInt32 Code;
                Core::JSON::DecUInt16 Key;
                Core::JSON::ArrayType<JSONModifier> Modifiers;
            };

        public:
            struct ConversionInfo {
                uint16_t Code;
                uint16_t Modifiers;
            };

        private:
            typedef std::map<const uint32_t, const ConversionInfo> LookupMap;

        public:
            typedef Core::IteratorMapType<const LookupMap, const ConversionInfo&, uint32_t, LookupMap::const_iterator> Iterator;

        public:
            KeyMap(KeyMap&&) = default;
            KeyMap(VirtualInput& parent)
                : _parent(parent)
                , _passThrough(false)
            {
            }
            ~KeyMap()
            {

                std::map<uint16_t, int16_t> removedKeys;

                while (_keyMap.size() > 0) {

                    // Negative reference counts
                    removedKeys[_keyMap.begin()->second.Code]--;

                    _keyMap.erase(_keyMap.begin());
                }

                ChangeIterator removed(removedKeys);
                _parent.MapChanges(removed);
            }

        public:
            inline bool PassThrough() const
            {
                return (_passThrough);
            }
            inline void PassThrough(const bool enabled)
            {
                _passThrough = enabled;
            }
            uint32_t Load(const string& mappingFile);
            uint32_t Save(const string& mappingFile);

            inline const ConversionInfo* operator[](const uint32_t code) const
            {
                std::map<const uint32_t, const ConversionInfo>::const_iterator index(_keyMap.find(code));

                return (index != _keyMap.end() ? &(index->second) : nullptr);
            }
            inline bool Add(const uint32_t code, const uint16_t key, const uint16_t modifiers)
            {
                bool added = false;
                std::map<const uint32_t, const ConversionInfo>::const_iterator index(_keyMap.find(code));

                if (index == _keyMap.end()) {
                    ConversionInfo element;
                    element.Code = key;
                    element.Modifiers = modifiers;

                    _keyMap.insert(std::pair<const uint32_t, const ConversionInfo>(code, element));
                    added = true;
                }
                return (added);
            }
            inline void Delete(const uint32_t code)
            {
                std::map<const uint32_t, const ConversionInfo>::const_iterator index(_keyMap.find(code));

                if (index != _keyMap.end()) {
                    _keyMap.erase(index);
                }
            }

            inline bool Modify(const uint32_t code, const uint16_t key, const uint16_t modifiers)
            {
                // Delete if exist
                Delete(code);

                return (Add(code, key, modifiers));
            }

        private:
            VirtualInput& _parent;
            LookupMap _keyMap;
            bool _passThrough;
        };

    public:
        enum actiontype {
            RELEASED = 0,
            PRESSED = 1,
            REPEAT = 2,
            COMPLETED = 3
        };

        struct INotifier {
            virtual ~INotifier() {}
            virtual void Dispatch(const actiontype action, const uint32_t code) = 0;
        };
        typedef std::map<const uint32_t, const uint32_t> PostLookupEntries;

    private:
        class PostLookupTable : public Core::JSON::Container {
        private:
            PostLookupTable(const PostLookupTable&) = delete;
            PostLookupTable& operator=(const PostLookupTable&) = delete;

        public:
            class Conversion : public Core::JSON::Container {
            private:
                Conversion& operator=(const Conversion&) = delete;

            public:
                class KeyCode : public Core::JSON::Container {
                private:
                    KeyCode& operator=(const KeyCode&) = delete;

                public:
                    KeyCode()
                        : Core::JSON::Container()
                        , Code()
                        , Mods()
                    {
                        Add(_T("code"), &Code);
                        Add(_T("mods"), &Mods);
                    }
                    KeyCode(const KeyCode& copy)
                        : Core::JSON::Container()
                        , Code()
                        , Mods()
                    {
                        Add(_T("code"), &Code);
                        Add(_T("mods"), &Mods);
                    }
                    virtual ~KeyCode()
                    {
                    }

                public:
                    Core::JSON::DecUInt16 Code;
                    Core::JSON::ArrayType<Core::JSON::EnumType<KeyMap::modifier>> Mods;
                };

            public:
                Conversion()
                    : Core::JSON::Container()
                    , In()
                    , Out()
                {
                    Add(_T("in"), &In);
                    Add(_T("out"), &Out);
                }
                Conversion(const Conversion& copy)
                    : Core::JSON::Container()
                    , In()
                    , Out()
                {
                    Add(_T("in"), &In);
                    Add(_T("out"), &Out);
                }
                virtual ~Conversion()
                {
                }

            public:
                KeyCode In;
                KeyCode Out;
            };

        public:
            PostLookupTable()
                : Core::JSON::Container()
                , Conversions()
            {
                Add(_T("conversion"), &Conversions);
            }
            ~PostLookupTable()
            {
            }

        public:
            Core::JSON::ArrayType<Conversion> Conversions;
        };

        typedef std::map<const string, KeyMap> TableMap;
        typedef std::vector<INotifier*> NotifierList;
        typedef std::map<uint32_t, NotifierList> NotifierMap;
        typedef std::map<const string, PostLookupEntries> PostLookupMap;

    public:
        VirtualInput(const VirtualInput&) = delete;
        VirtualInput& operator=(const VirtualInput&) = delete;    
        VirtualInput();
        virtual ~VirtualInput();

    public:
        inline void Interval(const uint16_t startTime, const uint16_t intervalTime, const uint16_t limit)
        {
            _repeatKey.Interval(startTime, intervalTime);
            _repeatLimit = limit;
        }

        virtual uint32_t Open() = 0;
        virtual uint32_t Close() = 0;
        void Default(const string& table)
        {

            if (table.empty() == true) {

                _defaultMap = nullptr;
            } else {
                TableMap::iterator index(_mappingTables.find(table));

                ASSERT(index != _mappingTables.end());

                if (index != _mappingTables.end()) {
                    _defaultMap = &(index->second);
                }
            }
        }

        KeyMap& Table(const string& table)
        {
            TableMap::iterator index(_mappingTables.find(table));
            if (index == _mappingTables.end()) {
                std::pair<TableMap::iterator, bool> result = _mappingTables.insert(std::make_pair(table, KeyMap(*this)));
                index = result.first;
            }

            ASSERT(index != _mappingTables.end());

            return (index->second);
        }

        inline void ClearTable(const std::string& name)
        {
            TableMap::iterator index(_mappingTables.find(name));

            if (index != _mappingTables.end()) {
                _mappingTables.erase(index);
            }
        }

        void Register(INotifier* callback, const uint32_t keyCode = ~0);
        void Unregister(const INotifier* callback, const uint32_t keyCode = ~0);

        // -------------------------------------------------------------------------------------------------------
        // Whenever a key is pressed or released, let this object know, it will take the proper arrangements and timings
        // to announce this key event to the linux system. Repeat event is triggered by the watchdog implementation
        // in this plugin. No need to signal this.
        uint32_t KeyEvent(const bool pressed, const uint32_t code, const string& tablename);

        typedef Core::IteratorMapType<const std::map<uint16_t, int16_t>, uint16_t, int16_t, std::map<uint16_t, int16_t>::const_iterator> ChangeIterator;

        void PostLookup(const string& linkName, const string& tableName)
        {
            Core::File data(tableName);
            if (data.Open(true) == true) {
                PostLookupTable info;
                info.FromFile(data);
                Core::JSON::ArrayType<PostLookupTable::Conversion>::Iterator index(info.Conversions.Elements());

                _lock.Lock();

                PostLookupMap::iterator postMap(_postLookupTable.find(linkName));
                if (postMap != _postLookupTable.end()) {
                    postMap->second.clear();
                } else {
                    auto newElement = _postLookupTable.emplace(std::piecewise_construct,
                        std::make_tuple(linkName),
                        std::make_tuple());
                    postMap = newElement.first;
                }
                while (index.Next() == true) {
                    if ((index.Current().In.IsSet() == true) && (index.Current().Out.IsSet() == true)) {
                        uint32_t from = index.Current().In.Code.Value();
                        uint32_t to = index.Current().Out.Code.Value();

                        from |= (Modifiers(index.Current().In.Mods) << 16);
                        to |= (Modifiers(index.Current().In.Mods) << 16);

                        postMap->second.insert(std::pair<const uint32_t, const uint32_t>(from, to));
                    }
                }

                if (postMap->second.size() == 0) {
                    _postLookupTable.erase(postMap);
                }

                LookupChanges(linkName);

                _lock.Unlock();
            }
        }
        inline const PostLookupEntries* FindPostLookup(const string& linkName) const
        {
            PostLookupMap::const_iterator linkMap(_postLookupTable.find(linkName));

            return (linkMap != _postLookupTable.end() ? &(linkMap->second) : nullptr);
        }

    private:
        virtual void MapChanges(ChangeIterator& updated) = 0;
        virtual void LookupChanges(const string&) = 0;

        void RepeatKey(const uint32_t code);
        void ModifierKey(const actiontype type, const uint16_t modifiers);
        bool SendModifier(const actiontype type, const enumModifier mode);
        void AdministerAndSendKey(const actiontype type, const uint32_t code);
        void DispatchRegisteredKey(const actiontype type, uint32_t code);

        virtual void SendKey(const actiontype type, const uint32_t code) = 0;

        inline uint16_t Modifiers(const Core::JSON::ArrayType<Core::JSON::EnumType<KeyMap::modifier>>& modifiers) const
        {
            uint16_t result = 0;
            Core::JSON::ArrayType<Core::JSON::EnumType<KeyMap::modifier>>::ConstIterator index(modifiers.Elements());

            while (index.Next() == true) {
                KeyMap::modifier element(index.Current());
                result |= element;
            }

            return (result);
        }

    protected:
        Core::CriticalSection _lock;

    private:
        Core::ProxyObject<RepeatKeyTimer> _repeatKey;
        uint32_t _modifiers;
        std::map<const string, KeyMap> _mappingTables;
        KeyMap* _defaultMap;
        NotifierList _notifierList;
        NotifierMap _notifierMap;
        PostLookupMap _postLookupTable;
        string _keyTable;
        uint32_t _pressedCode;
        uint16_t _repeatCounter;
        uint16_t _repeatLimit;
    };

#if !defined(__WIN32__) && !defined(__APPLE__)
    class EXTERNAL LinuxKeyboardInput : public VirtualInput {
    private:
        LinuxKeyboardInput(const LinuxKeyboardInput&) = delete;
        LinuxKeyboardInput& operator=(const LinuxKeyboardInput&) = delete;

    public:
        LinuxKeyboardInput(const string& source, const string& inputName);
        virtual ~LinuxKeyboardInput();

        virtual uint32_t Open();
        virtual uint32_t Close();
        virtual void MapChanges(ChangeIterator& updated);

    private:
        virtual void SendKey(const actiontype type, const uint32_t code);
        bool Updated(ChangeIterator& updated);
        virtual void LookupChanges(const string&);

    private:
        struct uinput_user_dev _uidev;
        int _eventDescriptor;
        const string _source;
        std::map<uint16_t, uint16_t> _deviceKeys;
    };
#endif

    class EXTERNAL IPCKeyboardInput : public VirtualInput {
    private:
        struct KeyData {
            VirtualInput::actiontype Action;
            uint32_t Code;
        };

        typedef Core::IPCMessageType<0, KeyData, Core::Void> KeyMessage;
        typedef Core::IPCMessageType<1, Core::Void, Core::IPC::Text<20>> NameMessage;

        IPCKeyboardInput(const IPCKeyboardInput&) = delete;
        IPCKeyboardInput& operator=(const IPCKeyboardInput&) = delete;

    public:
        class KeyboardLink : public Core::IDispatchType<Core::IIPC> {
        private:
            KeyboardLink(const KeyboardLink&) = delete;
            KeyboardLink& operator=(const KeyboardLink&) = delete;

        public:
            KeyboardLink(Core::IPCChannelType<Core::SocketPort, KeyboardLink>*)
                : _enabled(false)
                , _name()
                , _parent(nullptr)
                , _postLookup(nullptr)
                , _replacement(Core::ProxyType<KeyMessage>::Create())
            {
            }
            virtual ~KeyboardLink()
            {
            }

        public:
            inline void Enable(const bool enabled)
            {
                _enabled = enabled;
            }
            inline Core::ProxyType<Core::IIPC> InvokeAllowed(const Core::ProxyType<Core::IIPC>& element) const
            {
                Core::ProxyType<Core::IIPC> result;

                if (_enabled == true) {
                    if (_postLookup == nullptr) {
                        result = element;
                    } else {
                        KeyMessage& copy(static_cast<KeyMessage&>(*element));

                        ASSERT(dynamic_cast<KeyMessage*>(&(*element)) != nullptr);

                        // See if we need to convert this keycode..
                        PostLookupEntries::const_iterator index(_postLookup->find(copy.Parameters().Code));
                        if (index == _postLookup->end()) {
                            result = element;
                        } else {

                            _replacement->Parameters().Action = copy.Parameters().Action;
                            _replacement->Parameters().Code = index->second;
                            result = _replacement;
                        }
                    }
                }
                return (result);
            }
            inline const string& Name() const
            {
                return (_name);
            }
            inline void Parent(IPCKeyboardInput& parent)
            {
                // We assume it will only be set, if the client reports it self in, once !
                ASSERT(_parent == nullptr);
                _parent = &parent;
            }
            inline void Reload()
            {
                _postLookup = _parent->FindPostLookup(_name);
            }

        private:
            virtual void Dispatch(Core::IIPC& element) override
            {
                ASSERT(dynamic_cast<NameMessage*>(&element) != nullptr);

                _name = (static_cast<NameMessage&>(element).Response().Value());
                _enabled = true;
                _postLookup = _parent->FindPostLookup(_name);
            }

        private:
            bool _enabled;
            string _name;
            IPCKeyboardInput* _parent;
            const PostLookupEntries* _postLookup;
            Core::ProxyType<KeyMessage> _replacement;
        };

        class VirtualInputChannelServer : public Core::IPCChannelServerType<KeyboardLink, true> {
        private:
            typedef Core::IPCChannelServerType<KeyboardLink, true> BaseClass;

        public:
            VirtualInputChannelServer(IPCKeyboardInput& parent, const Core::NodeId& sourceName)
                : BaseClass(sourceName, 32)
                , _parent(parent)
            {
            }

            virtual void Added(Core::ProxyType<Client>& client) override
            {
                TRACE_L1("VirtualInputChannelServer::Added -- %d", __LINE__);

                Core::ProxyType<Core::IIPC> message(Core::ProxyType<NameMessage>::Create());

                // TODO: The reference to this should be held by the IPC mechanism.. Testing showed it did
                //       not, to be further investigated..
                message.AddRef();

                client->Extension().Parent(_parent);
                client->Invoke(message, &(client->Extension()));
            }

        private:
            IPCKeyboardInput& _parent;
        };

    public:
        IPCKeyboardInput(const Core::NodeId& sourceName);
        virtual ~IPCKeyboardInput();

        virtual uint32_t Open();
        virtual uint32_t Close();
        virtual void MapChanges(ChangeIterator& updated);
        virtual void LookupChanges(const string&);

    private:
        virtual void SendKey(const actiontype type, const uint32_t code);

    private:
        VirtualInputChannelServer _service;
    };

    class EXTERNAL InputHandler {
    private:
        InputHandler(const InputHandler&) = delete;
        InputHandler& operator=(const InputHandler&) = delete;

    public:
        InputHandler()
        {
        }
        ~InputHandler()
        {
        }

        enum type {
            DEVICE,
            VIRTUAL
        };

    public:
        void Initialize(const type t, const string& locator)
        {
            ASSERT(_inputHandler == nullptr);
#if defined(__WIN32__) || defined(__APPLE__)
            ASSERT(t == VIRTUAL)
            _inputHandler = new PluginHost::IPCKeyboardInput(Core::NodeId(locator.c_str()));
            TRACE_L1("Creating a IPC Channel for key communication. %d", 0);
#else
            if (t == VIRTUAL) {
                _inputHandler = new PluginHost::IPCKeyboardInput(Core::NodeId(locator.c_str()));
                TRACE_L1("Creating a IPC Channel for key communication. %d", 0);
            } else {
                if (Core::File(locator, false).Exists() == true) {
                    TRACE_L1("Creating a /dev/input device for key communication. %d", 0);

                    // Seems we have a possibility to use /dev/input, create it.
                    _inputHandler = new PluginHost::LinuxKeyboardInput(locator, _T("remote_input"));
                }
            }
#endif
            if (_inputHandler != nullptr) {
                TRACE_L1("Opening VirtualInput %s", locator.c_str());
                if (_inputHandler->Open() != Core::ERROR_NONE) {
                    TRACE_L1("ERROR: Could not open VirtualInput %s.", locator.c_str());
                }
            } else {
                TRACE_L1("ERROR: Could not create '%s' as key communication channel.", locator.c_str());
            }
        }

        void Deinitialize()
        {
            if (_inputHandler != nullptr) {
                _inputHandler->Close();
                delete (_inputHandler);
                _inputHandler = nullptr;
            }
        }

        static VirtualInput* KeyHandler()
        {
            return _inputHandler;
        }

    private:
        static VirtualInput* _inputHandler;
    };
} // PluginHost

namespace Core {

    template <>
    EXTERNAL /* static */ const EnumerateConversion<PluginHost::VirtualInput::KeyMap::modifier>*
    EnumerateType<PluginHost::VirtualInput::KeyMap::modifier>::Table(const uint16_t);

} // namespace Core

} // namespace WPEFramework

#endif // KeyHandler
