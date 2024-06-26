//////////////////////////////////////////////////////////////////////
//                                                                  //
// Part of the PCIT-CPP, under the Apache License v2.0              //
// You may not use this file except in compliance with the License. //
// See `http://www.apache.org/licenses/LICENSE-2.0` for info        //
//                                                                  //
//////////////////////////////////////////////////////////////////////


#include "../include/Context.h"

#include "./Tokenizer.h"

namespace pcit::panther{


	Context::Context(DiagnosticCallback diagnostic_callback, const Config& _config) noexcept 
		: callback(diagnostic_callback), config(_config) {
		evo::debugAssert(this->config.maxNumErrors > 0, "Max num errors cannot be 0");
	};


	Context::~Context() noexcept {
		if(this->isMultiThreaded() && this->threadsRunning()){
			this->shutdownThreads();
		}
	};
	

	auto Context::optimalNumThreads() noexcept -> evo::uint {
		return std::thread::hardware_concurrency() - 1;
	};



	auto Context::threadsRunning() const noexcept -> bool {
		evo::debugAssert(this->isMultiThreaded(), "Context is not set to be multi-threaded");

		if(this->workers.empty()){ return false; }

		return this->shutting_down_threads.test() == false;
	};


	auto Context::startupThreads() noexcept -> void {
		evo::debugAssert(this->isMultiThreaded(), "Context is not set to be multi-threaded");
		evo::debugAssert(this->threadsRunning() == false, "Threads already running");

		this->workers.reserve(this->config.numThreads);

		static constexpr auto worker_controller_impl = [](
			std::stop_token stop_token, Worker& worker
		) noexcept -> void {
			while(stop_token.stop_requested() == false){
				worker.get_task();
			};

			worker.done();
		};
			

		for(size_t i = 0; i < this->config.numThreads; i+=1){
			Worker& new_worker = this->workers.emplace_back(this);

			auto worker_controller = [context = this, &new_worker](std::stop_token stop_token) noexcept -> void {
				worker_controller_impl(stop_token, new_worker);
			};

			new_worker.getThread() = std::jthread(worker_controller);
			this->num_threads_running += 1;
			new_worker.getThread().detach();
		}

		this->emitDebug("pcit::panther::Context started up threads");
	};

	auto Context::shutdownThreads() noexcept -> void {
		evo::debugAssert(this->isMultiThreaded(), "Context is not set to be multi-threaded");
		evo::debugAssert(this->workers.empty() == false, "Threads are not running");

		const bool already_shutting_down = this->shutting_down_threads.test_and_set();
		if(already_shutting_down){ return; }
		EVO_DEFER([&]() noexcept -> void { this->shutting_down_threads.clear(); } );

		if(this->workers.empty()){ return; }

		for(Worker& worker : workers){
			worker.getThread().request_stop();
		}

		while(this->num_threads_running != 0){};

		this->workers.clear();

		this->task_group_running = false;

		this->emitDebug("pcit::panther::Context shutdown threads");
	};


	auto Context::waitForAllTasks() noexcept -> void {
		evo::debugAssert(this->isMultiThreaded(), "Context is not set to be multi-threaded");
		evo::debugAssert(this->threadsRunning(), "Threads are not running");
		evo::debugAssert(
			this->hasHitFailCondition() == false, "Context hit a fail condition, threads should be shutdown instead"
		);

		if(this->shutting_down_threads.test()){ return; }

		while(this->tasks.empty() == false){
			std::this_thread::sleep_for(std::chrono::milliseconds(32));
		};

		while(true){
			bool all_done = true;

			for(const Worker& worker : this->workers){
				if(worker.isWorking()){
					all_done = false;
					break;
				}
			}

			if(all_done){
				break;
			}

			std::this_thread::sleep_for(std::chrono::milliseconds(32));
		};

		this->task_group_running = false;
	};




	auto Context::loadFiles(evo::ArrayProxy<fs::path> file_paths) noexcept -> void {
		evo::debugAssert(
			this->isSingleThreaded() || this->threadsRunning(),
			"Context is set to be multi-threaded, but threads are not running"
		);

		evo::debugAssert(this->task_group_running == false, "Task group already running");


		this->task_group_running = true;

		// TODO: maybe check if any files had been loaded yet
		this->getSourceManager().reserveSources(file_paths.size());

		for(const fs::path& file_path : file_paths){
			this->tasks.emplace(std::make_unique<Task>(LoadFileTask(file_path)));
		}

		if(this->isSingleThreaded()){
			this->consume_tasks_single_threaded();
		}
	};


	auto Context::tokenizeLoadedFiles() noexcept -> void {
		evo::debugAssert(this->task_group_running == false, "Task group already running");

		{
			const auto lock_guard = std::lock_guard(this->src_manager_mutex);
			
			this->task_group_running = true;

			for(Source& source : this->src_manager.sources){
				this->tasks.emplace(std::make_unique<Task>(TokenizeFileTask(source.getID())));
			}
		}

		if(this->isSingleThreaded()){
			this->consume_tasks_single_threaded();
		}
	};




	auto Context::emit_diagnostic_impl(const Diagnostic& diagnostic) noexcept -> void {
		const auto lock_guard = std::lock_guard(this->callback_mutex);

		this->callback(*this, diagnostic);
	};


	auto Context::notify_task_errored() noexcept -> void {
		if(this->num_errors >= this->config.maxNumErrors){
			this->hit_fail_condition = true;
		}

		if(this->hasHitFailCondition() && this->isMultiThreaded()){
			// in a separate thread to prevent process from dead-locking when waiting for threads
			//  	(the worker thread that calls this function is never able to call its `done` function)
			std::thread([&]() noexcept -> void {
				this->shutdownThreads();
			}).detach();
		}
	};


	auto Context::consume_tasks_single_threaded() noexcept -> void {
		evo::debugAssert(this->isSingleThreaded(), "Context is not set to be single threaded");

		auto worker = Worker(this);

		while(this->tasks.empty() == false && this->hasHitFailCondition() == false){
			worker.get_task_single_threaded();
		};

		this->task_group_running = false;
	};


	//////////////////////////////////////////////////////////////////////
	// Worker


	auto Context::Worker::done() noexcept -> void {
		this->is_working = false;
		this->context->num_threads_running -= 1;
	};


	auto Context::Worker::get_task() noexcept -> void {
		evo::debugAssert(this->context->isMultiThreaded(), "Context is not set to be multi-threaded");

		auto task = std::unique_ptr<Task>();

		{
			const auto lock_guard = std::lock_guard(this->context->tasks_mutex);

			if(this->context->tasks.empty() == false){
				task.reset(this->context->tasks.front().release());
				this->context->tasks.pop();
			}else{
				this->context->task_group_running = false;
			}
		}

		if(task == nullptr){
			this->is_working = false;
			std::this_thread::sleep_for(std::chrono::milliseconds(32));

		}else{
			this->is_working = true;
			this->run_task(*task);
		}
	};


	auto Context::Worker::get_task_single_threaded() noexcept -> void {
		evo::debugAssert(this->context->isSingleThreaded(), "Context is not set to be single-threaded");

		this->is_working = true;

		if(this->context->tasks.empty() == false){
			auto task = std::unique_ptr<Task>(this->context->tasks.front().release());
			this->context->tasks.pop();
			this->run_task(*task);
		}

		this->is_working = false;
	};


	auto Context::Worker::run_task(const Task& task) noexcept -> void {
		const bool run_task_res = task.visit([&](auto& value) noexcept -> bool {
			using ValueT = std::decay_t<decltype(value)>;

			     if constexpr(std::is_same_v<ValueT, LoadFileTask>){     return this->run_load_file(value);     }
			else if constexpr(std::is_same_v<ValueT, TokenizeFileTask>){ return this->run_tokenize_file(value); }
		});

		if(run_task_res == false){
			this->context->notify_task_errored();
		}	
	};



	auto Context::Worker::run_load_file(const LoadFileTask& task) noexcept -> bool {
		if(evo::fs::exists(task.path.string()) == false){
			this->context->num_errors += 1;
			this->context->emit_diagnostic_internal(
				Diagnostic::Level::Error, Diagnostic::Code::MiscFileDoesNotExist, std::nullopt,
				std::format("File \"{}\" does not exist", task.path.string())
			);
			return false;
		}

		auto file = evo::fs::File();

		const bool open_res = file.open(task.path.string(), evo::fs::FileMode::Read);
		if(open_res == false){
			file.close();
			this->context->num_errors += 1;
			this->context->emit_diagnostic_internal(
				Diagnostic::Level::Error, Diagnostic::Code::MiscLoadFileFailed, std::nullopt,
				std::format("Failed to load file: \"{}\"", task.path.string())
			);
			return false;
		}

		const evo::Result<std::string> data_res = file.read();
		file.close();
		if(data_res.isError()){
			this->context->num_errors += 1;
			this->context->emit_diagnostic_internal(
				Diagnostic::Level::Error, Diagnostic::Code::MiscLoadFileFailed, std::nullopt,
				std::format("Failed to load file: \"{}\"", task.path.string())
			);
			return false;
		}

		this->context->emitTrace("Loaded file: \"{}\"", task.path.string());

		const auto lock_guard = std::lock_guard(this->context->src_manager_mutex);
		this->context->getSourceManager().addSource(std::move(task.path), std::move(data_res.value()));
		return true;
	};



	auto Context::Worker::run_tokenize_file(const TokenizeFileTask& task) noexcept -> bool {
		auto tokenizer = Tokenizer(*this->context, task.source_id);

		evo::Result<TokenBuffer> result = tokenizer.tokenize();
		if(result.isError()){ return false; }

		const SourceManager& source_manager = this->context->getSourceManager();
		const Source& source = source_manager.getSource(task.source_id);
		std::construct_at(&source.token_buffer, std::move(result.value()));

		this->context->emitTrace("Tokenized file: \"{}\"", source.getLocationAsString());
		return true;
	};



};