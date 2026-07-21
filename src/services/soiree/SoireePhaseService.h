#pragma once

#include <string>
#include <vector>
#include <optional>
#include <cstdint>

#include "../../models/SoireePhase.h"

namespace BeatMate::Services::Soiree {

class SoireePhaseService {
public:
    SoireePhaseService();  // ctor seeds default templates

    std::vector<Models::PhaseTemplate> listTemplates() const;
    std::optional<Models::PhaseTemplate> getTemplate(const std::string& name) const;

    // Adds a template. Returns false if a template with the same name already
    bool addTemplate(const Models::PhaseTemplate& t);

    bool saveToFile(const std::string& path) const;
    bool loadFromFile(const std::string& path);

    void setClientRequests(const Models::ClientRequests& r) { m_clientReq = r; }
    Models::ClientRequests clientRequests() const { return m_clientReq; }

    bool isMustPlay(int64_t trackId) const;
    bool isDoNotPlay(int64_t trackId) const;

private:
    static bool isValidTemplate(const Models::PhaseTemplate& t);
    void seedDefaults();

    std::vector<Models::PhaseTemplate> m_templates;
    Models::ClientRequests             m_clientReq;
};

} // namespace BeatMate::Services::Soiree
