from datetime import datetime

from sqlalchemy import JSON, String
from sqlalchemy.ext.asyncio import async_sessionmaker, create_async_engine
from sqlalchemy.orm import DeclarativeBase, Mapped, mapped_column

engine = create_async_engine("sqlite+aiosqlite:///telemetria.db")
Session = async_sessionmaker(engine, expire_on_commit=False)


class Base(DeclarativeBase):
    pass


class Telemetria(Base):
    __tablename__ = "telemetria"

    id: Mapped[int] = mapped_column(primary_key=True)
    node_id: Mapped[str] = mapped_column(String(64))
    sequence_id: Mapped[int]
    variables_json: Mapped[dict] = mapped_column(JSON)
    diagnostico: Mapped[str | None] = mapped_column(String(32))
    clase_predicha_rf: Mapped[str | None] = mapped_column(String(64))  # se llenará con el Random Forest
    latencia_ms: Mapped[float | None]
    fecha_registro: Mapped[datetime] = mapped_column(default=datetime.now)