/**
 * OBS Node.js Plugin TypeScript Type Definitions
 * 
 * Provides type definitions for the OBS JavaScript bindings exposed by obs-plugin-node.
 */

declare global {
    /**
     * Global OBS API object - available in scripts run by the OBS Node.js plugin.
     */
    const obs: OBS.API;

    /**
     * Last evaluated expression result (like Node.js REPL)
     */
    let _: unknown;
}

declare namespace OBS {
    // ============================================================================
    // Main API Interface
    // ============================================================================

    interface API {
        /** OBS version information */
        version: VersionInfo;
        /** OBS capabilities/build info */
        capabilities: Capabilities;
        /** Source management */
        sources: SourcesAPI;
        /** Scene management */
        scenes: ScenesAPI;
        /** Scene item manipulation */
        sceneItems: SceneItemsAPI;
        /** Filter management */
        filters: FiltersAPI;
        /** Transition management */
        transitions: TransitionsAPI;
        /** Frontend (UI) operations */
        frontend: FrontendAPI;
        /** Canvas/video configuration */
        canvas: CanvasAPI;
        /** Access to OBS outputs (streaming/recording) */
        outputs: OutputsAPI;
        /** Access to encoders */
        encoders: EncodersAPI;
        /** Access to services */
        services: ServicesAPI;
        /** Data/settings manipulation */
        data: DataAPI;
        /** Property definition inspection */
        properties: PropertiesAPI;
        /** Audio control */
        audio: AudioAPI;
        /** Hotkey management */
        hotkeys: HotkeysAPI;
        /** Event subscriptions */
        events: EventsAPI;
        /** WebSocket vendor features */
        websocket: WebSocketAPI;
        /** Module/plugin information */
        modules: ModulesAPI;
    }

    // ============================================================================
    // Version & Capabilities
    // ============================================================================

    interface VersionInfo {
        /** Full version string (e.g., "31.0.0") */
        string: string;
        /** Major version number */
        major: number;
        /** Minor version number */
        minor: number;
        /** Patch version number */
        patch: number;
    }

    interface Capabilities {
        /** Has OBS frontend API (UI) */
        frontend: boolean;
        /** Has obs-scripting module */
        scripting: boolean;
        /** Has obs-browser module */
        browser: boolean;
        /** Platform: 'windows', 'macos', or 'linux' */
        platform: 'windows' | 'macos' | 'linux';
    }

    // ============================================================================
    // Sources API
    // ============================================================================

    interface SourcesAPI {
        /** List all source names */
        list(): string[];

        /** Get source info by name */
        get(name: string): SourceInfo | null;

        /** Get available source type IDs */
        getTypes(): string[];

        /** Enable or disable a source */
        setEnabled(name: string, enabled: boolean): boolean;

        /** Mute or unmute a source */
        setMuted(name: string, muted: boolean): boolean;

        /** Rename a source */
        setName(oldName: string, newName: string): boolean;
    }

    interface SourceInfo {
        /** Source name */
        name: string;
        /** Source type ID (e.g., "browser_source", "image_source") */
        id: string;
        /** Source type enum value */
        type: number;
        /** Source width in pixels */
        width: number;
        /** Source height in pixels */
        height: number;
        /** Whether source is enabled */
        enabled: boolean;
        /** Whether source is active (being rendered) */
        active: boolean;
        /** Whether source is currently showing */
        showing: boolean;
        /** Whether source is muted */
        muted: boolean;
    }

    // ============================================================================
    // Scenes API
    // ============================================================================

    interface ScenesAPI {
        /** List all scene names */
        list(): string[];

        /** Get scene info by name */
        get(name: string): SceneInfo | null;

        /** Create a new scene */
        create(name: string): boolean;
    }

    interface SceneInfo {
        /** Scene name */
        name: string;
        /** Array of source names in this scene */
        items: string[];
    }

    // ============================================================================
    // Scene Items API
    // ============================================================================

    interface SceneItemsAPI {
        /** List all items in a scene */
        list(sceneName: string): SceneItemInfo[];

        /** Set visibility of a scene item */
        setVisible(sceneName: string, sourceName: string, visible: boolean): boolean;

        /** Check if scene item is visible */
        isVisible(sceneName: string, sourceName: string): boolean;

        /** Set locked state of a scene item */
        setLocked(sceneName: string, sourceName: string, locked: boolean): boolean;

        /** Get transform info of a scene item */
        getTransform(sceneName: string, sourceName: string): TransformInfo | null;

        /** Set position of a scene item */
        setPosition(sceneName: string, sourceName: string, x: number, y: number): boolean;

        /** Set scale of a scene item */
        setScale(sceneName: string, sourceName: string, scaleX: number, scaleY: number): boolean;

        /** Set rotation of a scene item */
        setRotation(sceneName: string, sourceName: string, rotation: number): boolean;

        /** Remove an item from a scene */
        remove(sceneName: string, sourceName: string): boolean;

        /** Add a source to a scene. Returns scene item ID */
        add(sceneName: string, sourceName: string): number;

        /** Set item order. 0=Top, 1=Bottom, 2=Up, 3=Down */
        setOrder(sceneName: string, sourceName: string, order: 0 | 1 | 2 | 3): boolean;

        /** Set item alignment */
        setAlignment(sceneName: string, sourceName: string, alignment: number): boolean;

        /** Set item bounds */
        setBounds(sceneName: string, sourceName: string, x: number, y: number, alignment: number, type: number): boolean;
    }

    interface SceneItemInfo {
        name: string;
        id: number;
        visible: boolean;
        locked: boolean;
    }

    interface TransformInfo {
        posX: number;
        posY: number;
        rotation: number;
        scaleX: number;
        scaleY: number;
        boundsX: number;
        boundsY: number;
    }

    // ============================================================================
    // Filters API
    // ============================================================================

    interface FiltersAPI {
        /** List filters on a source */
        list(sourceName: string): FilterInfo[];

        /** Get available filter type IDs */
        getTypes(): string[];

        /** Add a filter to a source */
        add(sourceName: string, filterName: string, filterTypeId: string): boolean;

        /** Remove a filter from a source */
        remove(sourceName: string, filterName: string): boolean;

        /** Enable or disable a filter */
        setEnabled(sourceName: string, filterName: string, enabled: boolean): boolean;

        /** Reorder a filter */
        reorder(sourceName: string, filterName: string, newIndex: number): boolean;
    }

    interface FilterInfo {
        name: string;
        id: string;
        enabled: boolean;
    }

    // ============================================================================
    // Transitions API
    // ============================================================================

    interface TransitionsAPI {
        /** List available transitions */
        list(): string[];

        /** Get available transition type IDs */
        getTypes(): string[];

        /** Get current transition name */
        getCurrent(): string | null;

        /** Set current transition by name */
        setCurrent(name: string): boolean;

        /** Get transition duration in ms */
        getDuration(): number;

        /** Set transition duration in ms */
        setDuration(ms: number): boolean;
    }

    // ============================================================================
    // Frontend API
    // ============================================================================


    interface FrontendAPI {
        /** Streaming controls */
        streaming: StreamingAPI;
        /** Recording controls */
        recording: RecordingAPI;
        /** Virtual camera controls */
        virtualCam: VirtualCamAPI;
        /** Replay buffer controls */
        replay: ReplayAPI;

        /** Get current program scene name */
        getCurrentScene(): string | null;

        /** Set current program scene by name */
        setCurrentScene(name: string): boolean;

        /** Get current preview scene name (studio mode) */
        getPreviewScene(): string | null;

        /** Set preview scene by name (studio mode) */
        setPreviewScene(name: string): boolean;

        /** Check if studio mode is enabled */
        isStudioMode(): boolean;

        /** Enable or disable studio mode */
        setStudioMode(enabled: boolean): void;
    }

    interface StreamingAPI {
        /** Start streaming */
        start(): boolean;
        /** Stop streaming */
        stop(): boolean;
        /** Check if currently streaming */
        isActive(): boolean;
    }

    interface RecordingAPI {
        /** Start recording */
        start(): boolean;
        /** Stop recording */
        stop(): boolean;
        /** Check if currently recording */
        isActive(): boolean;
        /** Pause recording */
        pause(): boolean;
        /** Unpause recording */
        unpause(): boolean;
        /** Check if recording is paused */
        isPaused(): boolean;
    }

    interface VirtualCamAPI {
        /** Start virtual camera */
        start(): boolean;
        /** Stop virtual camera */
        stop(): boolean;
        /** Check if virtual camera is active */
        isActive(): boolean;
    }

    interface ReplayAPI {
        /** Start replay buffer */
        start(): boolean;
        /** Stop replay buffer */
        stop(): boolean;
        /** Save current replay buffer */
        save(): boolean;
        /** Check if replay buffer is active */
        isActive(): boolean;
    }

    // ============================================================================
    // Canvas API
    // ============================================================================

    interface CanvasAPI {
        // Legacy video info functions
        /** Get base (canvas) resolution */
        getBaseResolution(): Resolution | null;

        /** Get output (scaled) resolution */
        getOutputResolution(): Resolution | null;

        /** Get FPS configuration */
        getFps(): FpsInfo | null;

        /** Get full video configuration */
        getVideoInfo(): VideoInfo | null;

        /** List all output names */
        getOutputs(): string[];

        /** Get output info by name */
        getOutput(name: string): OutputInfo | null;

        // OBS 31+ multi-canvas functions
        // OBS 31+ multi-canvas functions
        /** Get main canvas info (OBS 31+) */
        getMain(): CanvasInfo | null;

        /** List all canvas names (OBS 31+) */
        list(): string[];

        /** Get canvas info by name (OBS 31+) */
        get(name: string): CanvasInfo | null;

        /** Get scenes belonging to a canvas (OBS 31+) */
        getScenes(canvasName: string): string[];

        /** Rename a canvas (OBS 31+) */
        setName(oldName: string, newName: string): boolean;
    }

    interface Resolution {
        width: number;
        height: number;
    }

    interface FpsInfo {
        /** FPS numerator */
        num: number;
        /** FPS denominator */
        den: number;
        /** Calculated FPS value */
        fps: number;
    }

    interface VideoInfo {
        baseWidth: number;
        baseHeight: number;
        outputWidth: number;
        outputHeight: number;
        fpsNum: number;
        fpsDen: number;
        fps: number;
        colorspace: number;
        range: number;
        gpuConversion: number;
        scaleType: number;
    }

    interface OutputInfo {
        name: string;
        id: string;
        width: number;
        height: number;
        active: boolean;
        totalFrames: number;
        droppedFrames: number;
    }

    interface CanvasInfo {
        name: string;
        uuid: string;
        flags: number;
        hasVideo: boolean;
        removed: boolean;
        baseWidth?: number;
        baseHeight?: number;
        outputWidth?: number;
        outputHeight?: number;
    }

    // ============================================================================
    // Events API
    // ============================================================================

    /** Available event names */
    type EventName =
        | 'sceneChanged'
        | 'sceneListChanged'
        | 'previewSceneChanged'
        | 'streamingStarting'
        | 'streamingStarted'
        | 'streamingStopping'
        | 'streamingStopped'
        | 'recordingStarting'
        | 'recordingStarted'
        | 'recordingStopping'
        | 'recordingStopped'
        | 'recordingPaused'
        | 'recordingUnpaused'
        | 'replayBufferStarted'
        | 'replayBufferStopped'
        | 'replayBufferSaved'
        | 'studioModeEnabled'
        | 'studioModeDisabled'
        | 'virtualCamStarted'
        | 'virtualCamStopped'
        | 'exit';

    /** Event callback function */
    type EventCallback = (...args: string[]) => void;

    interface EventsAPI {
        /** Subscribe to an event */
        on(eventName: EventName, callback: EventCallback): boolean;

        /** Unsubscribe all callbacks for an event */
        off(eventName: EventName): boolean;

        /** List available event names */
        list(): EventName[];

        /** Manually process pending events (mainly for testing) */
        process(): void;
    }

    // ============================================================================
    // Outputs API
    // ============================================================================

    interface OutputsAPI {
        /** List all outputs */
        list(): OutputInfo[];
        /** Get output by name */
        get(name: string): OutputInfo | null;
        /** Get available output types */
        getTypes(): string[];
        /** Start an output */
        start(name: string): boolean;
        /** Stop an output */
        stop(name: string): boolean;
        /** Check if output is active */
        isActive(name: string): boolean;
    }

    interface OutputInfo {
        name: string;
        id: string;
        width: number;
        height: number;
        active: boolean;
        reconnecting: boolean;
        totalFrames: number;
        droppedFrames: number;
        totalBytes: number;
        congestion?: number;
    }

    // ============================================================================
    // Encoders API
    // ============================================================================

    interface EncodersAPI {
        /** List all active encoders */
        list(): EncoderInfo[];
        /** Get encoder by name */
        get(name: string): EncoderInfo | null;
        /** Get available video encoder types */
        getVideoTypes(): string[];
        /** Get available audio encoder types */
        getAudioTypes(): string[];
    }

    interface EncoderInfo {
        name: string;
        id: string;
        codec: string;
        type: 'video' | 'audio';
        width: number;
        height: number;
        sampleRate: number;
        active: boolean;
    }

    // ============================================================================
    // Services API
    // ============================================================================

    interface ServicesAPI {
        /** List all services */
        list(): ServiceInfo[];
        /** Get current streaming service */
        getCurrent(): ServiceInfo | null;
        /** Get available service types */
        getTypes(): string[];
    }

    interface ServiceInfo {
        name: string;
        id: string;
        type: string;
        url?: string;
        key?: string;
    }

    // ============================================================================
    // Data/Settings API
    // ============================================================================

    interface DataAPI {
        /** Get settings for a source */
        getSourceSettings(sourceName: string): any;
        /** Set settings for a source */
        setSourceSettings(sourceName: string, settings: any): boolean;
        /** Get settings for a filter */
        getFilterSettings(sourceName: string, filterName: string): any;
        /** Set settings for a filter */
        setFilterSettings(sourceName: string, filterName: string, settings: any): boolean;
        /** Get source settings as JSON string */
        toJSON(sourceName: string): string | null;
    }

    // ============================================================================
    // Properties API
    // ============================================================================

    interface PropertiesAPI {
        /** Get property definitions for a source instance */
        get(sourceName: string): PropertyDefinition[];
        /** Get property definitions for a source type ID (e.g. 'image_source') */
        create(sourceTypeId: string): PropertyDefinition[];
    }

    interface PropertyDefinition {
        name: string;
        description: string;
        type: 'bool' | 'int' | 'float' | 'text' | 'path' | 'list' | 'color' | 'button' | 'font' | 'editable_list' | 'frame_rate' | 'group' | 'invalid' | 'unknown';
        enabled: boolean;
        visible: boolean;

        // Number specific
        min?: number;
        max?: number;
        step?: number;

        // Path specific
        filter?: string;
        pathType?: 'file' | 'directory' | 'save_file';

        // List specific
        listType?: 'list' | 'editable';
        options?: { name: string; disabled?: boolean }[];
    }

    // ============================================================================
    // Audio API
    // ============================================================================

    interface AudioAPI {
        /** Get current audio monitoring device ID */
        getMonitoringDevice(): string;
        /** Set audio monitoring device by name and ID */
        setMonitoringDevice(name: string, id: string): boolean;

        /** Set source volume in dB */
        setSourceVolume(sourceName: string, db: number): boolean;
        /** Get source volume in dB */
        getSourceVolume(sourceName: string): number | null;

        /** Set source sync offset in nanoseconds */
        setSourceSyncOffset(sourceName: string, offsetNs: number): boolean;

        /** Set source monitoring type. 0=None, 1=Monitor Only, 2=Monitor+Output */
        setSourceMonitoringType(sourceName: string, type: 0 | 1 | 2): boolean;
        /** Get source monitoring type. 0=None, 1=Monitor Only, 2=Monitor+Output */
        getSourceMonitoringType(sourceName: string): 0 | 1 | 2 | null;
    }

    // ============================================================================
    // Hotkeys API
    // ============================================================================

    interface HotkeysAPI {
        /** 
         * Register a new hotkey.
         * @param name Unique internal name
         * @param description User-visible description
         * @param callback Function to call when hotkey is triggered
         * @returns ID of the registered hotkey
         */
        register(name: string, description: string, callback: (event: HotkeyEvent) => void): number;

        /** Unregister a hotkey by ID */
        unregister(id: number): void;
    }

    interface HotkeyEvent {
        pressed: boolean;
    }

    // ============================================================================
    // WebSocket API
    // ============================================================================

    interface WebSocketAPI {
        /**
         * Emit a custom vendor event to connected WebSocket clients.
         * @param vendor Vendor name
         * @param type Event type
         * @param data Event data
         */
        emit(vendor: string, type: string, data: object): boolean;

        /**
         * Register a vendor request handler.
         * @param vendor Vendor name
         * @param type Request type
         * @param callback Function to handle request. Receives request data, returns response data object.
         */
        on(vendor: string, type: string, callback: (data: any) => any): boolean;
    }

    // ============================================================================
    // Modules API
    // ============================================================================

    interface ModulesAPI {
        /** List all loaded modules/plugins */
        list(): ModuleInfo[];

        /** Get info for a specific module by name or file name */
        get(name: string): ModuleInfo | null;

        /** Get data file path for a module */
        getDataPath(moduleName: string, file: string): string | null;

        /** Get config file path for a module */
        getConfigPath(moduleName: string, file: string): string | null;
    }

    interface ModuleInfo {
        name: string;
        fileName: string;
        author: string;
        description: string;
        binaryPath: string;
        dataPath: string;
    }
}

// ============================================================================
// Module Declarations for require('obs') and require('obs:*')
// ============================================================================

declare module 'obs' {
    const obs: OBS.API;
    export = obs;
}

declare module 'obs:sources' {
    const sources: OBS.SourcesAPI;
    export = sources;
}

declare module 'obs:scenes' {
    const scenes: OBS.ScenesAPI;
    export = scenes;
}

declare module 'obs:sceneItems' {
    const sceneItems: OBS.SceneItemsAPI;
    export = sceneItems;
}

declare module 'obs:filters' {
    const filters: OBS.FiltersAPI;
    export = filters;
}

declare module 'obs:transitions' {
    const transitions: OBS.TransitionsAPI;
    export = transitions;
}

declare module 'obs:frontend' {
    const frontend: OBS.FrontendAPI;
    export = frontend;
}

declare module 'obs:canvas' {
    const canvas: OBS.CanvasAPI;
    export = canvas;
}

declare module 'obs:events' {
    const events: OBS.EventsAPI;
    export = events;
}

declare module 'obs:modules' {
    const modules: OBS.ModulesAPI;
    export = modules;
}

declare module 'obs:outputs' {
    const outputs: OBS.OutputsAPI;
    export = outputs;
}

declare module 'obs:encoders' {
    const encoders: OBS.EncodersAPI;
    export = encoders;
}

declare module 'obs:services' {
    const services: OBS.ServicesAPI;
    export = services;
}

declare module 'obs:data' {
    const data: OBS.DataAPI;
    export = data;
}

declare module 'obs:properties' {
    const properties: OBS.PropertiesAPI;
    export = properties;
}

declare module 'obs:audio' {
    const audio: OBS.AudioAPI;
    export = audio;
}

declare module 'obs:hotkeys' {
    const hotkeys: OBS.HotkeysAPI;
    export = hotkeys;
}

declare module 'obs:websocket' {
    const websocket: OBS.WebSocketAPI;
    export = websocket;
}

export { };
